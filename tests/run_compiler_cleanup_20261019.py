#!/usr/bin/env python3
"""Isolated bounded cleanup contracts; Python fixtures only, no compilers.

Linux supervisor containment is test-only. The helper must not kill escaped
children: the supervisor verifies their survival, then uses recorded pidfds to
rescue them. All assertions inspect helper state BEFORE supervisor rescue.
"""
import contextlib
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import compiler_process as helper

OUT = b'parent-out\x00\xff\n'
ERR = b'parent-err\x00\xfe\n'
CHILD = b'inherited-out\x00\xfd\n'
BLOCK = bytes(range(256)) * 256
SIZE = 32 * len(BLOCK)  # two MiB on each stream, substantially above pipe capacity


def fixture(mode, registry):
    path = Path(registry)
    def publish(data):
        # Pin identities before exit; rescue also checks start time after
        # pidfd_open so a recycled numeric PID is never a kill target.
        for name in ('leader', 'child'):
            pid = data.get(name)
            if pid:
                data[name + '_start'] = Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()[19]
        tmp = path.with_suffix('.tmp')
        tmp.write_text(json.dumps(data)); tmp.replace(path)
    publish({'leader': os.getpid(), 'child': None, 'ready': False})
    if mode.startswith('volume'):
        for _ in range(32):
            os.write(1, BLOCK); os.write(2, BLOCK[::-1])
        if mode == 'volume_timeout':
            time.sleep(60)
        return 0
    os.write(1, OUT); os.write(2, ERR)
    if mode == 'normal':
        return 1
    if mode == 'closed_pipes':
        os.close(1); os.close(2)
        publish({'leader': os.getpid(), 'child': None, 'ready': True})
        time.sleep(60)
        return 0
    r, w = os.pipe()
    child = os.fork()
    if not child:
        os.close(r)
        if mode.startswith('escaped'):
            os.setsid()
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        if mode in ('stdout_only', 'escaped_stdout'):
            os.close(2)
        os.write(1, CHILD)
        os.write(w, b'R'); os.close(w)
        time.sleep(60)
        os._exit(0)
    os.close(w)
    assert os.read(r, 1) == b'R'; os.close(r)
    publish({'leader': os.getpid(), 'child': child, 'ready': True})
    if mode in ('leader_exit', 'stdout_only', 'escaped', 'escaped_stdout'):
        return 0
    time.sleep(60)
    return 0


def summarize(data):
    return {'length': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'hex': data.hex() if len(data) < 1000 else None}


def worker(mode, registry, action):
    command = [sys.executable, str(Path(__file__).resolve()), '--fixture', mode, registry]
    timeout = .8 if mode.startswith('volume') else .2
    caught = None
    thread = None
    if action == 'cancel':
        def interrupt_when_ready():
            until = time.monotonic() + 1.5
            while time.monotonic() < until:
                try:
                    if json.loads(Path(registry).read_text())['ready']:
                        os.kill(os.getpid(), signal.SIGINT)
                        return
                except (FileNotFoundError, json.JSONDecodeError):
                    pass
                time.sleep(.002)
        thread = threading.Thread(target=interrupt_when_ready, daemon=True)
        thread.start()
    cm = patch.object(helper.os, 'killpg', side_effect=PermissionError('injected EPERM')) if action == 'kill_error' else contextlib.nullcontext()
    with cm:
        try:
            if action == 'cli':
                with patch.object(sys, 'argv', ['compiler_process.py', '--timeout', str(timeout), '--', *command]):
                    return helper.main()
            if action == 'run_compiler':
                with contextlib.redirect_stdout(io.StringIO()):
                    helper.run_compiler(command, 'source bytes', 'isolated fixture', timeout)
            result = helper.run(command, timeout=timeout)
            report = {'kind': 'completed', 'returncode': result.returncode,
                      'stdout': summarize(result.stdout), 'stderr': summarize(result.stderr)}
        except (subprocess.TimeoutExpired, KeyboardInterrupt, SystemExit) as error:
            caught = error
            if isinstance(error, SystemExit):
                report = {'kind': 'compiler_error', 'message': str(error)}
            else:
                report = {'kind': type(error).__name__,
                          'stdout': summarize(getattr(error, 'cleanup_stdout', error.output if isinstance(error, subprocess.TimeoutExpired) else b'')),
                          'stderr': summarize(getattr(error, 'cleanup_stderr', error.stderr if isinstance(error, subprocess.TimeoutExpired) else b'')),
                          'capture_incomplete': error.cleanup_capture_incomplete,
                          'reap_incomplete': error.cleanup_reap_incomplete,
                          'cleanup_errors': error.cleanup_errors,
                          'warning': helper._cleanup_warning(error)}
    if thread:
        thread.join(timeout=.2)
    print(json.dumps(report), flush=True)
    return 0


class RealCleanupTests(unittest.TestCase):
    evidence = []

    @classmethod
    def setUpClass(cls):
        if sys.platform != 'linux' or not hasattr(os, 'pidfd_open'):
            raise unittest.SkipTest('real fixture supervisor needs Linux procfs/pidfd/subreaper')
        cls.libc = ctypes.CDLL(None, use_errno=True)
        old = ctypes.c_int()
        if cls.libc.prctl(37, ctypes.byref(old), 0, 0, 0):  # GET_CHILD_SUBREAPER
            raise RuntimeError('cannot inspect test supervisor subreaper')
        cls.old_subreaper = old.value
        if cls.libc.prctl(36, 1, 0, 0, 0):
            raise RuntimeError('cannot enable test supervisor subreaper')

    @classmethod
    def tearDownClass(cls):
        if cls.libc.prctl(36, cls.old_subreaper, 0, 0, 0):
            raise RuntimeError('cannot restore test supervisor subreaper')

    @staticmethod
    def states(metadata):
        result = {}
        for name in ('leader', 'child'):
            pid = metadata.get(name)
            if pid:
                try:
                    result[name] = Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()[0]
                except FileNotFoundError:
                    result[name] = 'absent'
        return result

    def invoke(self, mode, action='run'):
        with tempfile.TemporaryDirectory(prefix='szc-cleanup-') as directory:
            registry = Path(directory) / 'pids.json'
            started = time.monotonic()
            p = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--worker', mode, str(registry), action],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            watchdog = False
            metadata = {}
            try:
                try:
                    stdout, stderr = p.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    watchdog = True
                    os.killpg(p.pid, signal.SIGKILL)
                    stdout, stderr = p.communicate(timeout=.5)
                elapsed = time.monotonic() - started
                if registry.exists():
                    metadata = json.loads(registry.read_text())
                states = self.states(metadata)
            finally:
                # Registry is owned by this fixture; pidfds bind rescue signals
                # to those identities. No process-name or global descendant scan.
                if not metadata and registry.exists():
                    metadata = json.loads(registry.read_text())
                for name in ('leader', 'child'):
                    pid = metadata.get(name)
                    if pid:
                        try:
                            fd = os.pidfd_open(pid)
                            try:
                                try:
                                    identity = Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()[19]
                                except FileNotFoundError:
                                    continue
                                if identity != metadata[name + '_start']:
                                    raise AssertionError('fixture PID identity changed; refusing rescue signal')
                                signal.pidfd_send_signal(fd, signal.SIGKILL)
                            finally:
                                os.close(fd)
                        except ProcessLookupError:
                            pass
                until = time.monotonic() + .5
                while time.monotonic() < until:
                    try:
                        pid, _ = os.waitpid(-1, os.WNOHANG)
                    except ChildProcessError:
                        break
                    if not pid:
                        time.sleep(.002)
                survivors = {name: state for name, state in self.states(metadata).items() if state != 'absent'}
            entry = {'mode': mode, 'action': action, 'elapsed_seconds': elapsed,
                     'watchdog': watchdog, 'worker_status': p.returncode,
                     'pre_rescue_states': states, 'survivors_after_rescue': survivors,
                     'stdout': summarize(stdout), 'stderr': summarize(stderr)}
            self.evidence.append(entry)
            self.assertFalse(watchdog, entry)
            self.assertFalse(survivors, entry)
            self.assertLess(elapsed, 2.5, entry)  # scheduling margin, not command budget
            if action == 'cli':
                self.assertEqual(p.returncode, 124)
                return stdout, stderr, states
            self.assertEqual(p.returncode, 0, stderr)
            self.assertEqual(stderr, b'')
            result = json.loads(stdout)
            entry['result'] = result
            return result, states

    def assert_bytes(self, result, stdout, stderr):
        self.assertEqual(result['stdout'], summarize(stdout))
        self.assertEqual(result['stderr'], summarize(stderr))

    def test_normal_exit_one_preserves_raw_bytes(self):
        r, states = self.invoke('normal')
        self.assertEqual((r['kind'], r['returncode']), ('completed', 1))
        self.assert_bytes(r, OUT, ERR)
        self.assertEqual(states['leader'], 'absent')

    def test_high_volume_both_streams_no_deadlock(self):
        r, _ = self.invoke('volume')
        self.assertEqual((r['kind'], r['returncode']), ('completed', 0))
        self.assert_bytes(r, BLOCK * 32, BLOCK[::-1] * 32)

    def test_high_volume_timeout_keeps_all_captured_bytes(self):
        r, states = self.invoke('volume_timeout')
        self.assertEqual(r['kind'], 'TimeoutExpired')
        self.assert_bytes(r, BLOCK * 32, BLOCK[::-1] * 32)
        self.assertFalse(r['capture_incomplete'])
        self.assertFalse(r['reap_incomplete'])
        self.assertEqual(states['leader'], 'absent')

    def test_in_group_holders_and_exited_leader(self):
        for mode in ('leader_live', 'leader_exit', 'stdout_only'):
            with self.subTest(mode=mode):
                r, states = self.invoke(mode)
                self.assertEqual(r['kind'], 'TimeoutExpired')
                self.assert_bytes(r, OUT + CHILD, ERR)
                self.assertFalse(r['capture_incomplete'])
                self.assertFalse(r['reap_incomplete'])
                self.assertFalse(r['cleanup_errors'])
                self.assertTrue(all(s in ('Z', 'absent') for s in states.values()), states)

    def test_eof_does_not_mean_leader_exited(self):
        r, states = self.invoke('closed_pipes')
        self.assertEqual(r['kind'], 'TimeoutExpired')
        self.assert_bytes(r, OUT, ERR)
        self.assertFalse(r['reap_incomplete'])
        self.assertEqual(states['leader'], 'absent')

    def test_escaped_holders_return_bounded_partial_bytes(self):
        for mode in ('escaped', 'escaped_stdout'):
            with self.subTest(mode=mode):
                r, states = self.invoke(mode)
                self.assertEqual(r['kind'], 'TimeoutExpired')
                self.assert_bytes(r, OUT + CHILD, ERR)
                self.assertTrue(r['capture_incomplete'])
                self.assertFalse(r['reap_incomplete'])
                self.assertIn('output capture did not reach EOF', r['warning'])
                self.assertNotIn(states['child'], ('Z', 'absent'))
                self.assertEqual(states['leader'], 'absent')

    def test_kill_error_is_bounded_and_separate(self):
        r, states = self.invoke('leader_live', 'kill_error')
        self.assertEqual(r['kind'], 'TimeoutExpired')
        self.assert_bytes(r, OUT + CHILD, ERR)
        self.assertTrue(r['capture_incomplete'])
        self.assertTrue(r['reap_incomplete'])
        self.assertIn('PermissionError', r['cleanup_errors'][0])
        self.assertNotIn(states['leader'], ('Z', 'absent'))

    def test_real_cancellation_cleans_original_group(self):
        r, states = self.invoke('leader_live', 'cancel')
        self.assertEqual(r['kind'], 'KeyboardInterrupt')
        self.assert_bytes(r, OUT + CHILD, ERR)
        self.assertFalse(r['reap_incomplete'])
        self.assertFalse(r['capture_incomplete'])
        self.assertTrue(all(s in ('Z', 'absent') for s in states.values()))

    def test_cli_timeout_warning_and_binary_output(self):
        stdout, stderr, _ = self.invoke('escaped_stdout', 'cli')
        self.assertEqual(stdout, OUT + CHILD)
        self.assertTrue(stderr.startswith(ERR))
        self.assertIn(b'compiler command timed out after 0.2 seconds', stderr)
        self.assertIn(b'cleanup incomplete: output capture did not reach EOF', stderr)

    def test_run_compiler_timeout_warns(self):
        r, _ = self.invoke('escaped_stdout', 'run_compiler')
        self.assertEqual(r['kind'], 'compiler_error')
        self.assertIn('timed out after 0.2 seconds', r['message'])
        self.assertIn('cleanup incomplete: output capture did not reach EOF', r['message'])
        self.assertIn('source bytes', r['message'])


class MockCleanupTests(unittest.TestCase):
    def process(self):
        p = Mock(pid=12345, returncode=0)
        p.stdout, p.stderr = Mock(), Mock()
        p.wait.return_value = 0
        return p

    def execute(self, p, primary, next_result=None, next_error=None, kill_error=None, clock=None):
        p.communicate.side_effect = [primary, next_error if next_error is not None else next_result]
        clock_cm = patch.object(helper.time, 'monotonic', side_effect=clock) if clock else contextlib.nullcontext()
        with patch.object(helper.subprocess, 'Popen', return_value=p) as spawn, \
                patch.object(helper.os, 'killpg', side_effect=kill_error) as kill, clock_cm:
            try:
                helper.run(['fixture'], timeout=7)
            except BaseException as caught:
                self.assertIs(caught, primary)
            else:
                self.fail('original exception was suppressed')
        spawn.assert_called_once_with(['fixture'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        kill.assert_called_once_with(p.pid, signal.SIGKILL)
        self.assertEqual(p.communicate.call_args_list[0].kwargs, {'timeout': 7})
        for call in p.communicate.call_args_list[1:] + p.wait.call_args_list:
            self.assertIn('timeout', call.kwargs)
            self.assertGreaterEqual(call.kwargs['timeout'], 0)
            self.assertLessEqual(call.kwargs['timeout'], helper._CLEANUP_TIMEOUT)
        p.stdout.close.assert_called_once()
        p.stderr.close.assert_called_once()
        return primary

    def test_latest_cumulative_snapshot_not_concatenated(self):
        p = self.process()
        first = subprocess.TimeoutExpired('fixture', 7, output=b'prefix\x00\xff', stderr=b'err')
        latest = subprocess.TimeoutExpired('fixture', .4, output=b'prefix\x00\xffTAIL', stderr=b'errTAIL')
        self.execute(p, first, next_error=latest)
        self.assertEqual(first.output, b'prefix\x00\xffTAIL')
        self.assertEqual(first.stderr, b'errTAIL')
        self.assertTrue(first.cleanup_capture_incomplete)
        self.assertFalse(first.cleanup_reap_incomplete)

    def test_missing_latest_fields_retain_prior_per_stream(self):
        for out, err in ((None, b'newerr'), (b'newout', None), (None, None), (b'', b'')):
            with self.subTest(out=out, err=err):
                first = subprocess.TimeoutExpired('fixture', 7, output=b'oldout', stderr=b'olderr')
                self.execute(self.process(), first, next_error=subprocess.TimeoutExpired('fixture', .4, output=out, stderr=err))
                self.assertEqual(first.output, b'oldout' if out is None else out)
                self.assertEqual(first.stderr, b'olderr' if err is None else err)

    def test_completed_drain_replaces_snapshots(self):
        error = subprocess.TimeoutExpired('fixture', 7, output=b'prefix', stderr=b'err')
        self.execute(self.process(), error, next_result=(b'prefixTAIL', b'errTAIL'))
        self.assertEqual((error.output, error.stderr), (b'prefixTAIL', b'errTAIL'))
        self.assertFalse(error.cleanup_capture_incomplete)
        self.assertFalse(helper._cleanup_warning(error))

    def test_missing_completed_drain_fields_keep_prior(self):
        error = subprocess.TimeoutExpired('fixture', 7, output=b'prefix', stderr=b'err')
        self.execute(self.process(), error, next_result=(None, None))
        self.assertEqual((error.output, error.stderr), (b'prefix', b'err'))

    def test_total_cleanup_clock_reserves_and_bounds_reap(self):
        p = self.process()
        p.wait.side_effect = subprocess.TimeoutExpired('fixture', .1)
        error = subprocess.TimeoutExpired('fixture', 7)
        self.execute(p, error, next_error=subprocess.TimeoutExpired('fixture', .1), clock=[10, 10.2, 10.45])
        self.assertAlmostEqual(p.communicate.call_args_list[1].kwargs['timeout'], .2)
        self.assertAlmostEqual(p.wait.call_args.kwargs['timeout'], .05)
        self.assertTrue(error.cleanup_reap_incomplete)

    def test_expired_cleanup_does_not_restart_budget(self):
        p = self.process()
        error = subprocess.TimeoutExpired('fixture', 7, output=OUT, stderr=ERR)
        self.execute(p, error, clock=[10, 10.6, 10.7])
        self.assertEqual(p.communicate.call_count, 1)
        p.wait.assert_called_once_with(timeout=0)
        self.assertEqual((error.output, error.stderr), (OUT, ERR))
        self.assertTrue(error.cleanup_capture_incomplete)

    def test_sigkill_lookup_race_is_not_failure(self):
        e = subprocess.TimeoutExpired('fixture', 7)
        self.execute(self.process(), e, next_result=(b'', b''), kill_error=ProcessLookupError())
        self.assertEqual(e.cleanup_errors, ())

    def test_permission_error_does_not_replace_original(self):
        e = subprocess.TimeoutExpired('fixture', 7, output=OUT, stderr=ERR)
        self.execute(self.process(), e, next_error=subprocess.TimeoutExpired('fixture', .4), kill_error=PermissionError('no permission'))
        self.assertEqual((e.output, e.stderr), (OUT, ERR))
        self.assertIn('PermissionError', e.cleanup_errors[0])
        self.assertIn('SIGKILL process group', helper._cleanup_warning(e))

    def test_original_cancellation_and_oserror_identity_preserved(self):
        for error in (KeyboardInterrupt('original interrupt'), OSError('original read failure'), SystemExit(19)):
            with self.subTest(error=type(error).__name__):
                self.execute(self.process(), error, next_result=(OUT, ERR))
                self.assertEqual((error.cleanup_stdout, error.cleanup_stderr), (OUT, ERR))

    def test_cancellation_during_emergency_cleanup_is_secondary(self):
        for operation in ('kill', 'drain', 'close', 'reap'):
            with self.subTest(operation=operation):
                p = self.process()
                if operation == 'close': p.stdout.close.side_effect = KeyboardInterrupt('secondary close')
                if operation == 'reap': p.wait.side_effect = KeyboardInterrupt('secondary reap')
                first = KeyboardInterrupt('primary')
                self.execute(p, first, next_result=(OUT, ERR),
                             next_error=KeyboardInterrupt('secondary drain') if operation == 'drain' else None,
                             kill_error=KeyboardInterrupt('secondary kill') if operation == 'kill' else None)
                self.assertIn('KeyboardInterrupt', ' '.join(first.cleanup_errors))

    def test_unexpected_cleanup_exception_is_secondary(self):
        p = self.process(); first = KeyboardInterrupt('primary')
        with patch.object(helper.subprocess, 'Popen', return_value=p), \
                patch.object(helper, '_cleanup', side_effect=RuntimeError('unexpected cleanup failure')):
            p.communicate.side_effect = first
            with self.assertRaises(KeyboardInterrupt) as caught:
                helper.run(['fixture'])
        self.assertIs(caught.exception, first)
        self.assertTrue(first.cleanup_capture_incomplete)
        self.assertTrue(first.cleanup_reap_incomplete)
        self.assertIn('unexpected cleanup failure', first.cleanup_errors[0])

    def test_oserror_stays_primary_when_kill_close_and_reap_fail(self):
        p = self.process()
        p.stdout.close.side_effect = OSError('close failed')
        p.wait.side_effect = subprocess.TimeoutExpired('fixture', .1)
        original = OSError('primary read failed')
        self.execute(p, original, next_error=subprocess.TimeoutExpired('fixture', .4, output=OUT, stderr=ERR),
                     kill_error=PermissionError('group kill failed'))
        self.assertEqual((original.cleanup_stdout, original.cleanup_stderr), (OUT, ERR))
        self.assertTrue(original.cleanup_capture_incomplete)
        self.assertTrue(original.cleanup_reap_incomplete)
        self.assertIn('group kill failed', original.cleanup_errors[0])
        self.assertIn('close failed', original.cleanup_errors[1])

    def test_timeout_keeps_original_command_timeout_and_identity(self):
        command = ['compiler', 'file with spaces']
        original = subprocess.TimeoutExpired(command, 120, output=OUT, stderr=ERR)
        args = original.args
        self.execute(self.process(), original, next_result=(OUT, ERR))
        self.assertIs(original.cmd, command)
        self.assertEqual(original.timeout, 120)
        self.assertEqual(original.args, args)
        self.assertEqual((original.output, original.stderr), (OUT, ERR))

    def test_run_compiler_oserror_keeps_original_diagnostic_and_captured_bytes(self):
        # Rendering the wrapper must not hide the original error behind even
        # an empty captured stderr field. The error's raw captures stay exact.
        for stderr in (ERR, b''):
            with self.subTest(stderr=stderr):
                error = OSError('original post-spawn read explanation')
                error.cleanup_stdout = OUT
                error.cleanup_stderr = stderr
                error.cleanup_capture_incomplete = True
                error.cleanup_reap_incomplete = False
                error.cleanup_errors = ()
                command = ['compiler', 'file with spaces']
                with patch.object(helper, 'run', side_effect=error) as run, \
                        contextlib.redirect_stdout(io.StringIO()) as progress:
                    with self.assertRaises(SystemExit) as caught:
                        helper.run_compiler(command, 'source context', 'wrapper label', 120)
                text = str(caught.exception)
                self.assertTrue(text.startswith('wrapper label: could not start compiler:'))
                self.assertIn('original post-spawn read explanation', text)
                self.assertIn('cleanup incomplete: output capture did not reach EOF', text)
                self.assertIn("command: compiler 'file with spaces'", text)
                self.assertIn('source:\nsource context', text)
                self.assertTrue(text.endswith('stdout:\n' + OUT.decode(errors='replace') +
                                              '\nstderr:\n' + stderr.decode(errors='replace')))
                self.assertEqual((error.cleanup_stdout, error.cleanup_stderr), (OUT, stderr))
                self.assertIn('wrapper label', progress.getvalue())
                run.assert_called_once_with(command, timeout=120)

    def test_spawn_error_has_no_cleanup(self):
        first = FileNotFoundError('missing')
        with patch.object(helper.subprocess, 'Popen', side_effect=first), patch.object(helper.os, 'killpg') as kill:
            with self.assertRaises(FileNotFoundError) as caught:
                helper.run(['fixture'])
        self.assertIs(caught.exception, first)
        kill.assert_not_called()


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--fixture':
        sys.exit(fixture(*sys.argv[2:]))
    if len(sys.argv) > 1 and sys.argv[1] == '--worker':
        sys.exit(worker(*sys.argv[2:]))
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    destination = os.environ.get('SZC_CLEANUP_EVIDENCE')
    if destination:
        Path(destination).write_text(json.dumps({'tests_run': result.testsRun,
            'failures': len(result.failures), 'errors': len(result.errors),
            'skipped': len(result.skipped), 'real_cases': RealCleanupTests.evidence}, indent=2) + '\n')
    sys.exit(0 if result.wasSuccessful() else 1)
