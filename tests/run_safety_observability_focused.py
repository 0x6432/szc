#!/usr/bin/env python3
"""Focused mocked checks; never compile or execute generated safety programs."""
import contextlib
import io
import os
import resource
import signal
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import run_safety as safety


class ObservabilityTests(unittest.TestCase):
    fixture = ('probe', 'pub fn main() void {}\n', b'panic\x00\xff')

    def invoke(self, outcomes, zig=False):
        output = io.StringIO()
        env = {'ZIG': 'reference-compiler'} if zig else {}
        with patch.object(safety, 'cases', return_value=iter([self.fixture])), \
                patch.object(sys, 'argv', ['run_safety.py', 'candidate-compiler', '--case', 'probe']), \
                patch.dict(os.environ, env, clear=True), \
                patch.object(safety.resource, 'setrlimit') as limits, \
                patch.object(safety, 'run', side_effect=outcomes) as run, \
                patch.object(safety.time, 'monotonic', side_effect=[10 + i * .25 for i in range(12)]), \
                contextlib.redirect_stdout(output):
            status = safety.main()
        limits.assert_called_once_with(resource.RLIMIT_CORE, (0, 0))
        return status, output.getvalue(), run.call_args_list

    @staticmethod
    def completed(code=0, stdout=b'', stderr=b''):
        return subprocess.CompletedProcess([], code, stdout, stderr)

    def test_native_timeout_keeps_binary_evidence_and_phase(self):
        error = subprocess.TimeoutExpired(['native'], 15, output=b'out\x00\xff\x1b', stderr=b'panic\x00\xff\n')
        status, text, calls = self.invoke([self.completed(), error])
        self.assertEqual(status, 1)
        self.assertIn('phase=native elapsed=0.250000s', text)
        self.assertIn("stdout=b'out\\x00\\xff\\x1b'", text)
        self.assertIn("stderr=b'panic\\x00\\xff\\n'", text)
        self.assertIn('argv=', text)
        self.assertNotIn('\x1b', text)
        self.assertEqual(calls[0].kwargs, {'timeout': 120})
        self.assertEqual(calls[1].kwargs, {})  # shared run's default remains 15

    def test_compile_timeout_has_compile_argv_and_captured_bytes(self):
        error = subprocess.TimeoutExpired(['compiler'], 120, output=b'compile-out', stderr=b'compile-err')
        status, text, calls = self.invoke([error])
        self.assertEqual(status, 1)
        self.assertEqual(len(calls), 1)
        self.assertIn('phase=compile elapsed=0.250000s', text)
        self.assertIn('candidate-compiler', text)
        self.assertIn('build-exe', text)
        self.assertIn("stdout=b'compile-out'", text)
        self.assertIn("stderr=b'compile-err'", text)

    def test_timeout_truncation_is_explicit_and_bounded(self):
        error = subprocess.TimeoutExpired(['native'], 15, output=b'\xff' * 5000, stderr=b'\x00' * 5000)
        status, text, _ = self.invoke([self.completed(), error])
        self.assertEqual(status, 1)
        self.assertIn('[truncated: 2952 of 5000 bytes omitted]', text)
        self.assertLess(len(text), 18000)

    def test_no_output_timeout_is_handled(self):
        status, text, _ = self.invoke([subprocess.TimeoutExpired(['compiler'], 120)])
        self.assertEqual(status, 1)
        self.assertIn("stdout=b''", text)
        self.assertIn("stderr=b''", text)

    def test_valid_exact_binary_panic_passes(self):
        status, text, calls = self.invoke([self.completed(), self.completed(-signal.SIGABRT, stderr=b'panic\x00\xff\n')])
        self.assertEqual(status, 0)
        self.assertIn('safety: 1 passed, 0 failed', text)
        self.assertEqual(len(calls), 2)
        self.assertNotIn('FAIL', text)

    def test_signal_stdout_and_exact_message_checks_are_preserved(self):
        outcomes = [
            self.completed(0, stderr=b'panic\x00\xff\n'),
            self.completed(-signal.SIGTERM, stderr=b'panic\x00\xff\n'),
            self.completed(-signal.SIGABRT, stdout=b'not empty', stderr=b'panic\x00\xff\n'),
            self.completed(-signal.SIGABRT, stderr=b'panic\x00\xff'),
            self.completed(-signal.SIGABRT, stderr=b'extra panic\x00\xff\n'),
        ]
        for outcome in outcomes:
            with self.subTest(outcome=outcome):
                status, text, _ = self.invoke([self.completed(), outcome])
                self.assertEqual(status, 1)
                self.assertIn('phase=native', text)
                self.assertIn('safety: 0 passed, 1 failed', text)

    def test_compile_failure_keeps_output(self):
        status, text, calls = self.invoke([self.completed(1, b'compiler stdout', b'compiler stderr')])
        self.assertEqual(status, 1)
        self.assertEqual(len(calls), 1)
        self.assertIn('phase=compile', text)
        self.assertIn("stdout=b'compiler stdout'", text)
        self.assertIn("stderr=b'compiler stderr'", text)

    def test_reference_substring_semantics_preserved(self):
        exact = self.completed(-signal.SIGABRT, stderr=b'panic\x00\xff\n')
        reference = self.completed(-signal.SIGABRT, stderr=b'trace: panic\x00\xff with suffix')
        status, text, calls = self.invoke([self.completed(), exact, self.completed(), reference], zig=True)
        self.assertEqual(status, 0)
        self.assertEqual(len(calls), 4)
        self.assertIn('-j2', calls[2].args[0])
        self.assertIn('safety: 1 passed, 0 failed', text)
        status, text, _ = self.invoke([self.completed(), exact, self.completed(), self.completed(-signal.SIGABRT, stderr=b'wrong')], zig=True)
        self.assertEqual(status, 1)
        self.assertIn('reference panic message mismatch', text)

    def test_selection_default_and_shards_match_original_indices(self):
        all_cases = list(safety.cases())
        self.assertTrue(all_cases)
        self.assertEqual(list(safety.selected_cases(0, 1)), all_cases)
        for shard in range(4):
            self.assertEqual(list(safety.selected_cases(shard, 4)), all_cases[shard::4])
        names = ['negate_i64', 'cast_signed_high']
        expected = [case for case in all_cases if case[0] in names]
        self.assertEqual(list(safety.selected_cases(0, 1, names + names)), expected)
        for shard in range(4):
            expected = [case for case in all_cases[shard::4] if case[0] in names]
            if expected:
                self.assertEqual(list(safety.selected_cases(shard, 4, names)), expected)
            else:
                with self.assertRaisesRegex(ValueError, 'no requested'):
                    list(safety.selected_cases(shard, 4, names))

    def test_cli_rejects_unknown_and_empty_intersection(self):
        with contextlib.redirect_stderr(io.StringIO()):
            for argv in [['--case', 'missing'], ['--case', 'negate_i64', '--case', 'missing'], ['--case', 'negate_i64', '--shard-index', '999', '--shard-count', '1000']]:
                with self.subTest(argv=argv), self.assertRaises(SystemExit) as error:
                    safety.arguments(argv)
                self.assertEqual(error.exception.code, 2)
        selected = safety.arguments(['--case', 'negate_i64', '--case', 'cast_signed_high']).selected
        self.assertEqual({case[0] for case in selected}, {'negate_i64', 'cast_signed_high'})


if __name__ == '__main__':
    unittest.main()
