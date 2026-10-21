#!/usr/bin/env python3
"""Accounting/API unit tests; no fixture execution or real compiler discovery."""
import contextlib
import io
import json
import os
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_error_query_undefined_20261022 as checker
from check_error_query_undefined_20261022 import classify, outcome, load_cases, frozen_strict


def actual(status=1, primaries=None, nyi=False, process_ok=True):
    return dict(status=status, primaries=primaries or [], nyi=nyi, process_ok=process_ok)


class AccountingTests(unittest.TestCase):
    def setUp(self):
        self.ref = dict(status=1, primaries=['illegal undefined'])
        self.nyi = actual(primaries=['not supported by szc yet: finite error-set value or layout demand'], nyi=True)
        self.case = dict(reference=self.ref, baseline_candidate={k: self.nyi[k] for k in ('status', 'primaries')},
                         baseline_accounting='REFERENCE-INVALID-NYI')

    def test_exact_reject(self):
        self.assertEqual(classify(self.ref, actual(primaries=self.ref['primaries'])), 'EXACT-REJECT')

    def test_exact_accept(self):
        self.assertEqual(classify(dict(status=0, primaries=[]), actual(0)), 'EXACT-ACCEPT')

    def test_invalid_nyi_never_exact(self):
        self.assertEqual(classify(self.ref, self.nyi), 'REFERENCE-INVALID-NYI')
        self.assertFalse(outcome(self.case, self.nyi)[1])

    def test_valid_unsupported_separate(self):
        self.assertEqual(classify(dict(status=0, primaries=[]), self.nyi), 'REFERENCE-VALID-UNSUPPORTED')

    def test_same_rejection_status_not_parity(self):
        self.assertEqual(classify(self.ref, actual(primaries=['wrong diagnostic'])), 'PRIMARY-GAP')

    def test_accepting_invalid_is_failure(self):
        self.assertEqual(classify(self.ref, actual(0)), 'STATUS-GAP')

    def test_diagnostic_order_preserved(self):
        self.assertEqual(classify(dict(status=1, primaries=['a', 'b']), actual(primaries=['b', 'a'])), 'PRIMARY-GAP')

    def test_known_gap_opt_in_not_parity(self):
        self.assertEqual(outcome(self.case, self.nyi, True), ('REFERENCE-INVALID-NYI', True))

    def test_changed_gap_fails_policy(self):
        changed = actual(primaries=['not supported by szc yet: changed fence'], nyi=True)
        self.assertFalse(outcome(self.case, changed, True)[1])

    def test_improvement_is_allowed(self):
        self.assertEqual(outcome(self.case, actual(primaries=self.ref['primaries']), True), ('EXACT-REJECT', True))

    def test_process_failure_cannot_be_gap(self):
        broken = dict(self.nyi, process_ok=False)
        self.assertEqual(outcome(self.case, broken, True), ('PROCESS-FAILURE', False))

    def test_manifest_and_strict56_integrity(self):
        frozen_strict()
        self.assertEqual(len(load_cases()), 36)


class ApiTests(unittest.TestCase):
    def run_checker(self, arguments, *, wrong_version=False, drift=False):
        cases = {c['file']: c for c in load_cases()}
        discoveries, commands = [], []

        def discover(explicit, env, names):
            discoveries.append((explicit, env))
            self.assertIn(env, ('SZC', '__EXPLICIT_UNDEFINED_REFERENCE__'))
            if env != 'SZC':
                self.assertEqual(explicit, '/bin/true')
            return '/bin/true'

        def invoke(command, timeout):
            commands.append(command)
            if command[1] == 'version':
                return dict(actual(0), stdout='0.16.0\n' if wrong_version else '0.17.0\n', stderr='')
            self.assertIn(command[1], ('tir', 'build-obj'))
            case = cases[Path(command[2]).name]
            expected = case['baseline_candidate'] if command[1] == 'tir' else case['reference']
            primaries = expected['primaries']
            if drift and command[1] == 'build-obj':
                expected, primaries = dict(status=1), ['drifted reference']
            return dict(actual(expected['status'], primaries,
                               nyi=any('not supported by szc yet:' in p for p in primaries)),
                        stdout='', stderr='\n'.join('error: ' + p for p in primaries))

        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / 'report.json'
            with patch.dict(os.environ, {'ZIG': '/does/not/exist/poison-zig',
                                         '__EXPLICIT_UNDEFINED_REFERENCE__': '/also/poisoned'}), \
                    patch.object(checker, 'discover', side_effect=discover), \
                    patch.object(checker, 'invoke', side_effect=invoke), \
                    contextlib.redirect_stdout(io.StringIO()):
                code = checker.main([*arguments, '--report', str(report)])
            return code, json.loads(report.read_text()), discoveries, commands

    def test_candidate_only_strict_ignores_zig_environment(self):
        code, report, discoveries, commands = self.run_checker(['/bin/true'])
        self.assertEqual(code, 1)
        self.assertEqual(discoveries, [('/bin/true', 'SZC')])
        self.assertEqual(len(commands), 36)
        self.assertTrue(all(c[1] == 'tir' for c in commands))
        self.assertIs(report['reference_requested'], False)
        self.assertEqual(report['reference_basis'], 'frozen-0.17.0-observations')
        self.assertEqual(report['reference_validated'], 0)
        self.assertEqual(report['exact_reference'], 13)
        self.assertEqual(report['nonexact_reference'], 23)
        self.assertNotIn('version', report)
        self.assertNotIn('reference', report['binaries'])
        self.assertTrue(all(r['reference_validated'] is None and 'reference' not in r for r in report['records']))

    def test_candidate_only_known_gaps_opt_in(self):
        code, report, _, _ = self.run_checker(['/bin/true', '--allow-known-gaps'])
        self.assertEqual(code, 0)
        self.assertEqual(report['exact_reference'], 13)
        self.assertEqual(report['nonexact_reference'], 23)
        self.assertEqual(report['summary']['REFERENCE-INVALID-NYI'], 19)
        self.assertEqual(report['summary']['REFERENCE-VALID-UNSUPPORTED'], 2)
        self.assertEqual(report['accepted_by_policy'], 36)

    def test_reference_only_requires_explicit_zig_despite_environment(self):
        with patch.dict(os.environ, {'ZIG': '/bin/true'}), \
                patch.object(checker, 'discover') as discover, \
                patch.object(checker, 'invoke') as invoke, \
                contextlib.redirect_stderr(io.StringIO()) as stderr:
            with self.assertRaises(SystemExit) as raised:
                checker.main(['--reference-only'])
        self.assertEqual(raised.exception.code, 2)
        self.assertIn('--reference-only requires explicit --zig', stderr.getvalue())
        discover.assert_not_called()
        invoke.assert_not_called()

    def test_explicit_zig_revalidates_all_rows(self):
        code, report, discoveries, commands = self.run_checker(['/bin/true', '--zig', '/bin/true'])
        self.assertEqual(code, 1)
        self.assertEqual(len(discoveries), 2)
        self.assertEqual(len(commands), 73)
        self.assertIs(report['reference_requested'], True)
        self.assertEqual(report['reference_basis'], 'explicit-0.17.0-revalidation')
        self.assertEqual(report['reference_validated'], 36)
        self.assertEqual(report['exact_reference'], 13)
        self.assertTrue(all(r['reference_validated'] for r in report['records']))

    def test_explicit_reference_only_never_discovers_candidate(self):
        code, report, discoveries, commands = self.run_checker(['--reference-only', '--zig', '/bin/true'])
        self.assertEqual(code, 0)
        self.assertEqual(discoveries, [('/bin/true', '__EXPLICIT_UNDEFINED_REFERENCE__')])
        self.assertEqual(len(commands), 37)
        self.assertEqual(report['reference_validated'], 36)
        self.assertEqual(report['exact_reference'], 0)
        self.assertEqual(report['summary'], {'REFERENCE-VALIDATED': 36})
        self.assertNotIn('candidate', report['binaries'])

    def test_explicit_wrong_version_is_infrastructure_failure(self):
        code, report, _, commands = self.run_checker(['--reference-only', '--zig', '/bin/true'], wrong_version=True)
        self.assertEqual(code, 2)
        self.assertEqual(len(commands), 1)
        self.assertIn('reference must actually report0.17.0', report['infrastructure_error'])

    def test_reference_drift_not_hidden_by_known_gap_policy(self):
        code, report, _, _ = self.run_checker(['/bin/true', '--zig', '/bin/true', '--allow-known-gaps'], drift=True)
        self.assertEqual(code, 1)
        self.assertEqual(report['summary'], {'REFERENCE-FAILURE': 36})
        self.assertEqual(report['reference_validated'], 0)
        self.assertEqual(report['accepted_by_policy'], 0)


if __name__ == '__main__':
    unittest.main()
