#!/usr/bin/env python3
"""Containment/accounting tests; synthetic counter failures are never waived."""
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import check_anyerror_value_containment_20261022 as checker


def counts(reject=True, expected=None):
    return dict(frontend_ok=True, sema_ok=not reject, errors=int(reject),
                anyerror_comptime_values=0,
                **{k: 0 for k in checker.ZERO_KEYS},
                **{k: (expected or {}).get(k, 0) for k in checker.PUBLICATION_KEYS})


def observed(status=1, stdout='', primaries=None, process_ok=True):
    return dict(status=status, stdout=stdout, primaries=primaries or [], process_ok=process_ok,
                stderr='', nyi=bool(primaries))


FENCE = ['not supported by szc yet: anyerror value or layout demand']


class ContainmentTests(unittest.TestCase):
    def setUp(self):
        self.case = dict(policy='ANYERROR-FENCE', internal={}, reference=dict(status=0, primaries=[]))

    def test_real_anyerror_fence(self):
        self.assertTrue(checker.anyerror_fence(FENCE))
        self.assertFalse(checker.internal_failures(self.case, counts(), FENCE))

    def test_unrelated_nyi_not_an_anyerror_fence(self):
        self.assertFalse(checker.anyerror_fence(['not supported by szc yet: arrays']))
        self.assertFalse(checker.anyerror_fence(['anyerror has a problem']))
        self.assertFalse(checker.anyerror_fence([]))

    def test_accepted_anyerror_is_failure_even_with_empty_output(self):
        self.assertIn('emit-qbe-STATUS', checker.stage_failures(self.case, observed(0), 'emit-qbe'))

    def test_partial_qbe_is_failure(self):
        failures = checker.stage_failures(self.case, observed(stdout='call $sink()', primaries=FENCE), 'emit-qbe')
        self.assertIn('emit-qbe-PARTIAL-OUTPUT', failures)

    def test_empty_qbe_alone_is_not_proof(self):
        self.assertFalse(checker.stage_failures(self.case, observed(primaries=FENCE), 'emit-qbe'))
        failures = checker.internal_failures(self.case, dict(counts(), calls=1, sink_calls=1), FENCE)
        self.assertIn('INTERNAL-SINK_CALLS', failures)
        self.assertIn('INTERNAL-CALLS', failures)

    def test_failed_body_tir_still_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), anyerror_tir_values=1), FENCE)
        self.assertIn('INTERNAL-ANYERROR_TIR_VALUES', failures)

    def test_slot_escape_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), anyerror_allocations=1, allocations=1), FENCE)
        self.assertIn('INTERNAL-ANYERROR_ALLOCATIONS', failures)
        self.assertIn('INTERNAL-ALLOCATIONS', failures)

    def test_backing_escape_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), anyerror_backing_objects=1), FENCE)
        self.assertIn('INTERNAL-ANYERROR_BACKING_OBJECTS', failures)

    def test_runtime_signature_escape_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), active_anyerror_signatures=1), FENCE)
        self.assertIn('INTERNAL-ACTIVE_ANYERROR_SIGNATURES', failures)

    def test_global_escape_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), blocked_globals=1, runtime_globals=1), FENCE)
        self.assertIn('INTERNAL-BLOCKED_GLOBALS', failures)
        self.assertIn('INTERNAL-RUNTIME_GLOBALS', failures)

    def test_invalid_call_argument_fails(self):
        failures = checker.internal_failures(self.case, dict(counts(), invalid_call_arguments=1), FENCE)
        self.assertIn('INTERNAL-INVALID_CALL_ARGUMENTS', failures)

    def test_frontend_refusal_not_value_fence(self):
        failures = checker.internal_failures(self.case, dict(counts(), frontend_ok=False), FENCE)
        self.assertIn('INTERNAL-FRONTEND', failures)

    def test_missing_counters_fail_closed(self):
        self.assertIn('INTERNAL-ANYERROR_ALLOCATIONS', checker.internal_failures(self.case, {}, []))

    def test_positive_witness_requires_nonzero_events(self):
        case = dict(policy='ACCEPT', internal=dict(calls=1, sink_calls=1))
        good = counts(False, case['internal'])
        self.assertFalse(checker.internal_failures(case, good, []))
        self.assertIn('INTERNAL-SINK_CALLS', checker.internal_failures(case, dict(good, sink_calls=0), []))

    def test_nyi_not_accepted_as_type_only_control(self):
        case = dict(policy='ACCEPT', internal={})
        self.assertTrue(checker.stage_failures(case, observed(primaries=FENCE), 'tir'))

    def test_comptime_values_not_runtime_publication_claim(self):
        self.assertFalse(checker.internal_failures(self.case, dict(counts(), anyerror_comptime_values=1), FENCE))

    def test_process_failures_fail_closed(self):
        self.assertEqual(checker.stage_failures(self.case, observed(process_ok=False), 'tir'), ['tir-PROCESS'])

    def test_valid_unsupported_distinct_from_reference_invalid(self):
        self.assertEqual(checker.expected_classification(self.case), 'REFERENCE-VALID-UNSUPPORTED')
        self.case['reference'] = dict(status=1, primaries=['cannot pass anyerror'])
        self.assertEqual(checker.expected_classification(self.case), 'REFERENCE-INVALID-NYI-REFUSAL')

    def test_frozen_fixtures_and_original_strict56(self):
        checker.frozen_strict()
        self.assertEqual(len(checker.load_cases()), 27)

    def test_candidate_only_api_never_discovers_zig(self):
        by_file = {c['file']: c for c in checker.load_cases()}
        calls = []

        def invoke(command, timeout):
            calls.append(command)
            if command[-2] == '-o':
                Path(command[-1]).write_bytes(b'mocked-probe')
                return observed(0)
            if len(command) == 3 and command[1] in ('tir', 'emit-qbe'):
                case = by_file[Path(command[2]).name]
                reject = case['policy'] == 'ANYERROR-FENCE'
                return observed(int(reject), '' if reject else 'accepted output', FENCE if reject else [])
            self.assertEqual(len(command), 2)
            case = by_file[Path(command[1]).name]
            reject = case['policy'] == 'ANYERROR-FENCE'
            return observed(0, json.dumps(counts(reject, case['internal'])), FENCE if reject else [])

        with tempfile.TemporaryDirectory() as tmp:
            report_path = Path(tmp) / 'report.json'
            with patch.dict(os.environ, {'ZIG': '/poison/must/not/be/discovered'}), \
                    patch.object(checker, 'discover', return_value='/bin/true') as discover, \
                    patch.object(checker, 'invoke', side_effect=invoke), \
                    contextlib.redirect_stdout(io.StringIO()):
                code = checker.main(['/bin/true', '--report', str(report_path)])
            report = json.loads(report_path.read_text())
        self.assertEqual(code, 0)
        discover.assert_called_once()
        self.assertEqual(discover.call_args.args[1], 'SZC')
        self.assertEqual(len(calls), 82)  # one probe build +27*(tir/qbe/internal)
        self.assertIs(report['reference_requested'], False)
        self.assertEqual(report['reference_validated'], 0)
        self.assertNotIn('version', report)
        self.assertNotIn('reference', report['binaries'])
        self.assertTrue(all(r['reference_validated'] is None and 'reference' not in r for r in report['records']))
        self.assertEqual(report['nyi_exact_credit'], 0)
        self.assertEqual(report['exact_accepts'], 9)


if __name__ == '__main__':
    unittest.main()
