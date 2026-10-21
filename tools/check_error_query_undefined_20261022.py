#!/usr/bin/env python3
"""Compile-only undefined finite error-query regression (Zig 0.17.0).

Default exit status is strict reference parity. --allow-known-gaps permits only
byte-pinned baseline diagnostic snapshots, never credits those as exact parity.
Default runs use frozen reference observations; no Zig lookup, even via ZIG.
Only explicit --zig revalidates0.17.0. No native fixture execution.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import tempfile
from check_error_queries_20261019 import discover, invoke, positive, sha

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/error_query_undefined_20261022'
STRICT = ROOT / 'tests/error_query_contract_20261019'
STRICT_SHA = '572edc99094feeb2ba67d9d195280e75693ecaf6777b8665c8825da2853f72ea'
CASE_COUNT = 36


def matches(expected, actual):
    return (actual['process_ok'] and actual.get('status') == expected['status']
            and actual['primaries'] == expected['primaries'])


def classify(reference, actual):
    if not actual['process_ok']:
        return 'PROCESS-FAILURE'
    if matches(reference, actual) and not actual['nyi']:
        return 'EXACT-REJECT' if reference['status'] else 'EXACT-ACCEPT'
    if actual['nyi']:
        return 'REFERENCE-INVALID-NYI' if reference['status'] else 'REFERENCE-VALID-UNSUPPORTED'
    return 'STATUS-GAP' if actual.get('status') != reference['status'] else 'PRIMARY-GAP'


def outcome(case, actual, allow_known_gaps=False):
    accounting = classify(case['reference'], actual)
    if accounting.startswith('EXACT-'):
        return accounting, True
    known = (accounting == case['baseline_accounting'] and accounting != 'PROCESS-FAILURE'
             and matches(case['baseline_candidate'], actual))
    return accounting, bool(allow_known_gaps and known)


def frozen_strict():
    if sha(STRICT / 'manifest.json') != STRICT_SHA:
        raise ValueError('existing strict56 manifest changed')
    cases = json.loads((STRICT / 'manifest.json').read_text())['cases']
    if len(cases) != 56 or {p.name for p in STRICT.glob('*.zig')} != {c['file'] for c in cases}:
        raise ValueError('existing strict56 membership changed')
    for case in cases:
        if sha(STRICT / case['file']) != case['source_sha256']:
            raise ValueError('existing strict56 source changed: ' + case['file'])


def load_cases():
    manifest = json.loads((FIXTURES / 'manifest.json').read_text())
    cases = manifest['cases']
    if manifest['reference_version'] != '0.17.0' or manifest['baseline_commit'] != '466f1d7':
        raise ValueError('unexpected reference or baseline pin')
    if (len(cases) != CASE_COUNT or len({c['file'] for c in cases}) != CASE_COUNT
            or {p.name for p in FIXTURES.glob('*.zig')} != {c['file'] for c in cases}):
        raise ValueError('manifest must cover exactly36 distinct fixtures')
    for case in cases:
        if Path(case['file']).name != case['file'] or sha(FIXTURES / case['file']) != case['source_sha256']:
            raise ValueError('frozen fixture changed: ' + case['file'])
        for key in ('reference', 'baseline_candidate'):
            expectation = case[key]
            if expectation['status'] not in (0, 1) or bool(expectation['primaries']) != bool(expectation['status']):
                raise ValueError('invalid expected status/primaries')
        baseline = dict(case['baseline_candidate'], process_ok=True,
                        nyi=any('not supported by szc yet:' in p for p in case['baseline_candidate']['primaries']))
        if classify(case['reference'], baseline) != case['baseline_accounting']:
            raise ValueError('baseline accounting disagrees with exact reference')
    return cases


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', nargs='?')
    parser.add_argument('--zig', help='explicit Zig 0.17.0 executable; never discovered by default')
    parser.add_argument('--reference-only', action='store_true')
    parser.add_argument('--allow-known-gaps', action='store_true')
    parser.add_argument('--timeout', type=positive, default=20)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args(argv)
    if args.reference_only and not args.zig:
        parser.error('--reference-only requires explicit --zig')
    report = {'contract': 'error_query_undefined_20261022', 'stage': 'tir',
              'baseline_commit': '466f1d7', 'reference_pin': '0.17.0',
              'reference_requested': bool(args.zig),
              'reference_basis': 'explicit-0.17.0-revalidation' if args.zig else 'frozen-0.17.0-observations',
              'allow_known_gaps': args.allow_known_gaps, 'reference_only': args.reference_only,
              'scope': 'Compile only. Frozen reference observations unless explicit --zig revalidates them. '
                       'Exact ordered primary lists and exit status, not notes/caret parity. '
                       'NYI is never exact; no runtime errors, unions, backend, ABI or native proof.',
              'runner_sha256': sha(Path(__file__)),
              'helper_sha256': sha(ROOT / 'tools/check_error_queries_20261019.py'),
              'process_helper_sha256': sha(ROOT / 'tools/compiler_process.py'), 'records': []}

    def finish(code):
        report['exit_code'] = code
        report['summary'] = dict(Counter(r['accounting'] for r in report['records']))
        report['exact_reference'] = sum(r['accounting'].startswith('EXACT-') for r in report['records'])
        report['nonexact_reference'] = sum(not r['accounting'].startswith(('EXACT-', 'REFERENCE-VALIDATED')) for r in report['records'])
        report['reference_validated'] = sum(bool(r.get('reference_validated')) for r in report['records'])
        report['accepted_by_policy'] = sum(r['passed'] for r in report['records'])
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], sort_keys=True), flush=True)
        return code

    try:
        frozen_strict()
        cases = load_cases()
        manifest_hash = sha(FIXTURES / 'manifest.json')
        report['manifest_sha256'] = manifest_hash
        report['strict56_manifest_sha256'] = STRICT_SHA
        zig = discover(args.zig, '__EXPLICIT_UNDEFINED_REFERENCE__', []) if args.zig else None
        candidate = None if args.reference_only else discover(args.candidate, 'SZC', [ROOT / 'build/szc', 'szc'])
        binaries = {}
        if zig:
            binaries['reference'] = zig
        if candidate:
            binaries['candidate'] = candidate
        report['binaries'] = {k: {'path': p, 'sha256': sha(Path(p))} for k, p in binaries.items()}
        if zig:
            version = invoke([zig, 'version'], args.timeout)
            report['version'] = version
            if not version['process_ok'] or version.get('status') or version['stdout'].strip() != '0.17.0':
                raise ValueError('reference must actually report0.17.0')
        with tempfile.TemporaryDirectory(prefix='szc-undefined-query-') as tmp:
            for case in cases:
                source = FIXTURES / case['file']
                row = {'file': case['file'], 'category': case['category'], 'observation': case['observation'],
                       'source': source.read_text(), 'source_sha256': sha(source),
                       'expected_reference': case['reference'], 'baseline_candidate': case['baseline_candidate'],
                       'baseline_accounting': case['baseline_accounting']}
                row['reference_validated'] = None
                if zig:
                    row['reference'] = invoke([zig, 'build-obj', str(source), '-j2', '-fno-emit-bin',
                                               '--cache-dir', str(Path(tmp) / 'local'),
                                               '--global-cache-dir', str(Path(tmp) / 'global')], args.timeout)
                    row['reference_validated'] = matches(case['reference'], row['reference']) and not row['reference']['nyi']
                if candidate:
                    row['candidate'] = invoke([candidate, 'tir', str(source)], args.timeout)
                    row['accounting'], row['passed'] = outcome(case, row['candidate'], args.allow_known_gaps)
                else:
                    row['accounting'], row['passed'] = 'REFERENCE-VALIDATED', True
                if zig and not row['reference_validated']:
                    row['accounting'], row['passed'] = 'REFERENCE-FAILURE', False
                report['records'].append(row)
                print(row['accounting'] + ': ' + case['file'], flush=True)
        frozen_strict()
        load_cases()
        if sha(FIXTURES / 'manifest.json') != manifest_hash:
            raise ValueError('manifest changed during run')
        for key, path in binaries.items():
            report['binaries'][key]['sha256_after'] = sha(Path(path))
            if report['binaries'][key]['sha256'] != report['binaries'][key]['sha256_after']:
                raise ValueError(key + ' binary changed during run')
        return finish(int(not all(r['passed'] for r in report['records'])))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report['infrastructure_error'] = str(error)
        print('INFRASTRUCTURE: ' + str(error), flush=True)
        return finish(2)


if __name__ == '__main__':
    raise SystemExit(main())
