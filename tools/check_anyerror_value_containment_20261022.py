#!/usr/bin/env python3
"""Fail-closed anyerror VALUE containment; no Zig fixture is executed.

Default is candidate-only using frozen Zig0.17.0 observations: NEVER discover
Zig, even via ZIG. Explicit --zig revalidates every reference case. The native
C inspector executes only frontend analysis and scans all allocated TIR, slots,
constant backing and globals. Empty generated QBE alone is not this proof.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import shlex
import tempfile
from check_error_queries_20261019 import discover, invoke, positive, sha

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests/anyerror_value_containment_20261022'
STRICT = ROOT / 'tests/error_query_contract_20261019'
STRICT_SHA = '572edc99094feeb2ba67d9d195280e75693ecaf6777b8665c8825da2853f72ea'
CASE_COUNT = 27
LIBS = 'arena source diag intern lexer ast parser irgen comptime struct_layout sema codegen driver'.split()
ZERO_KEYS = ('anyerror_tir_values', 'anyerror_allocations', 'anyerror_backing_objects',
             'blocked_tir_values', 'blocked_allocations', 'blocked_backing_objects',
             'blocked_globals', 'active_anyerror_signatures', 'invalid_call_arguments')
PUBLICATION_KEYS = ('calls', 'sink_calls', 'allocations', 'stores', 'backing_objects', 'runtime_globals')


def expected_classification(case):
    if case['policy'] == 'ACCEPT':
        return 'ACCEPT'
    return 'REFERENCE-INVALID-NYI-REFUSAL' if case['reference']['status'] else 'REFERENCE-VALID-UNSUPPORTED'


def matches(expected, actual):
    return (actual['process_ok'] and actual.get('status') == expected['status']
            and actual['primaries'] == expected['primaries'])


def anyerror_fence(primaries):
    # Containment contract, not an exact-diagnostic/reference parity assertion.
    # Includes the preexisting global-anyerror refusal, which must stay fenced.
    return bool(primaries) and all(p.startswith('not supported by szc yet: ')
                                  and 'anyerror' in p for p in primaries)


def stage_failures(case, observed, stage):
    if not observed['process_ok']:
        return [stage + '-PROCESS']
    reject = case['policy'] == 'ANYERROR-FENCE'
    failures = []
    if observed.get('status') != int(reject):
        failures.append(stage + '-STATUS')
    if reject:
        if not anyerror_fence(observed['primaries']):
            failures.append(stage + '-ANYERROR-FENCE')
        if stage == 'emit-qbe' and observed['stdout']:
            failures.append(stage + '-PARTIAL-OUTPUT')
    elif observed['primaries'] or not observed['stdout'].strip():
        failures.append(stage + '-ACCEPT-OUTPUT')
    if '%4294967295' in observed['stdout']:
        failures.append(stage + '-INVALID-NONE')
    return failures


def internal_failures(case, counts, primaries):
    failures = []
    reject = case['policy'] == 'ANYERROR-FENCE'
    if counts.get('frontend_ok') is not True:
        failures.append('INTERNAL-FRONTEND')
    if counts.get('sema_ok') is not (not reject):
        failures.append('INTERNAL-SEMA-STATUS')
    if reject:
        if not isinstance(counts.get('errors'), int) or counts['errors'] <= 0 or not anyerror_fence(primaries):
            failures.append('INTERNAL-ANYERROR-FENCE')
    elif counts.get('errors') != 0 or primaries:
        failures.append('INTERNAL-ACCEPT-DIAGNOSTICS')
    for key in ZERO_KEYS:
        if counts.get(key) != 0:
            failures.append('INTERNAL-' + key.upper())
    # Isolated fixtures intentionally have no safe pre-fence effects. Positive
    # controls require real nonzero events to calibrate these same counters.
    for key in PUBLICATION_KEYS:
        expected = case['internal'].get(key, 0)
        if counts.get(key) != expected:
            failures.append('INTERNAL-' + key.upper())
    # Comptime interned values are reported, NOT conflated with runtime TIR or
    # actual publication. No zero-value-table proof is claimed.
    return failures


def frozen_strict():
    if sha(STRICT / 'manifest.json') != STRICT_SHA:
        raise ValueError('original strict56 manifest changed')
    cases = json.loads((STRICT / 'manifest.json').read_text())['cases']
    if len(cases) != 56 or {p.name for p in STRICT.glob('*.zig')} != {c['file'] for c in cases}:
        raise ValueError('original strict56 membership changed')
    for case in cases:
        if sha(STRICT / case['file']) != case['source_sha256']:
            raise ValueError('original strict56 source changed: ' + case['file'])


def load_cases():
    manifest = json.loads((FIXTURES / 'manifest.json').read_text())
    cases = manifest['cases']
    if manifest['reference_version'] != '0.17.0':
        raise ValueError('unexpected reference pin')
    if (len(cases) != CASE_COUNT or len({c['file'] for c in cases}) != CASE_COUNT
            or {p.name for p in FIXTURES.glob('*.zig')} != {c['file'] for c in cases}):
        raise ValueError('manifest must cover exactly27 distinct fixtures')
    for case in cases:
        if Path(case['file']).name != case['file'] or sha(FIXTURES / case['file']) != case['source_sha256']:
            raise ValueError('frozen fixture changed: ' + case['file'])
        if case['policy'] not in ('ACCEPT', 'ANYERROR-FENCE'):
            raise ValueError('unknown policy')
        expected = case['reference']
        if expected['status'] not in (0, 1) or bool(expected['primaries']) != bool(expected['status']):
            raise ValueError('invalid reference status/primary list')
        if case['policy'] == 'ACCEPT' and expected['status']:
            raise ValueError('positive control must be reference-valid')
        if case['classification'] != expected_classification(case):
            raise ValueError('classification disagrees with reference')
        if not set(case['internal']).issubset(PUBLICATION_KEYS):
            raise ValueError('unknown positive publication counter')
        if case['policy'] == 'ANYERROR-FENCE' and case['internal']:
            raise ValueError('refusal must have no publication')
    return cases


def source_hashes():
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted((ROOT / 'src').glob('*.[ch]'))}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', nargs='?')
    parser.add_argument('--sema-overlay', type=Path, help='Optional immutable sema.c snapshot for inspector only; records its hash')
    parser.add_argument('--zig', help='Optional explicit Zig0.17.0; no default Zig lookup')
    parser.add_argument('--timeout', type=positive, default=20)
    parser.add_argument('--build-timeout', type=positive, default=180)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args(argv)
    report = {'contract': 'anyerror_value_containment_20261022', 'records': [],
              'reference_requested': bool(args.zig), 'reference_pin': '0.17.0',
              'reference_basis': 'explicit-revalidation' if args.zig else 'frozen-observations',
              'runner_sha256': sha(Path(__file__)),
              'helper_sha256': sha(ROOT / 'tools/check_error_queries_20261019.py'),
              'process_helper_sha256': sha(ROOT / 'tools/compiler_process.py'),
              'scope': 'Containment, not NYI parity. No Zig fixture execution. Internal direct roots/array-child '
                       'edges only, all allocated TIR including failed bodies. No arbitrary pointer/struct graph, '
                       'layout/ABI/backend/native-safety proof. Probe proof applies to recorded source hashes; '
                       'candidate binary/source equivalence is not inferred from a filename.'}

    def finish(code):
        report['exit_code'] = code
        report['summary'] = dict(Counter(r['outcome'] for r in report['records']))
        report['classifications'] = dict(Counter(r['classification'] for r in report['records']))
        report['reference_validated'] = sum(bool(r.get('reference_validated')) for r in report['records'])
        report['exact_accepts'] = sum(r['classification'] == 'ACCEPT' and not r['failures'] for r in report['records'])
        report['nyi_exact_credit'] = 0
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], sort_keys=True), flush=True)
        return code

    try:
        frozen_strict()
        cases = load_cases()
        manifest_hash = sha(FIXTURES / 'manifest.json')
        probe_hash = sha(FIXTURES / 'publication_probe.c')
        before = source_hashes()
        overlay = args.sema_overlay.resolve() if args.sema_overlay else None
        overlay_hash = sha(overlay) if overlay else None
        if overlay:
            report['sema_overlay'] = {'path': str(overlay), 'sha256': overlay_hash}
        report.update(manifest_sha256=manifest_hash, probe_sha256=probe_hash,
                      source_sha256=before, original_strict56_manifest_sha256=STRICT_SHA)
        candidate = discover(args.candidate, 'SZC', [ROOT / 'build/szc', ROOT / 'build/asan/szc', 'szc'])
        zig = discover(args.zig, '__EXPLICIT_ANYERROR_REFERENCE__', []) if args.zig else None
        binaries = {'candidate': candidate}
        if zig:
            binaries['reference'] = zig
        report['binaries'] = {k: {'path': p, 'sha256': sha(Path(p))} for k, p in binaries.items()}
        if zig:
            version = report['version'] = invoke([zig, 'version'], args.timeout)
            if not version['process_ok'] or version.get('status') or version['stdout'].strip() != '0.17.0':
                raise ValueError('reference must actually report0.17.0')
        with tempfile.TemporaryDirectory(prefix='szc-anyerror-value-') as tmp:
            probe = Path(tmp) / 'publication-probe'
            command = (shlex.split(os.environ.get('CC', 'cc'))
                       + ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Wstrict-prototypes',
                          '-Wmissing-prototypes', '-Werror']
                       + shlex.split(os.environ.get('CFLAGS', '-O0 -g'))
                       + ['-I' + str(ROOT / 'src'), str(FIXTURES / 'publication_probe.c')]
                       + [str(overlay if name == 'sema' and overlay else ROOT / 'src' / (name + '.c')) for name in LIBS]
                       + ['-o', str(probe)])
            build = report['probe_build'] = invoke(command, args.build_timeout)
            if not build['process_ok'] or build.get('status') != 0:
                raise ValueError('internal probe build failed')
            report['probe_binary_sha256'] = sha(probe)
            for case in cases:
                source = FIXTURES / case['file']
                row = {'file': case['file'], 'classification': case['classification'],
                       'policy': case['policy'], 'category': case['category'], 'source': source.read_text(),
                       'source_sha256': sha(source), 'expected_reference': case['reference'],
                       'reference_validated': None, 'failures': []}
                if zig:
                    row['reference'] = invoke([zig, 'build-obj', str(source), '-j2', '-fno-emit-bin',
                                               '--cache-dir', str(Path(tmp) / 'local'),
                                               '--global-cache-dir', str(Path(tmp) / 'global')], args.timeout)
                    row['reference_validated'] = matches(case['reference'], row['reference']) and not row['reference']['nyi']
                    if not row['reference_validated']:
                        row['failures'].append('REFERENCE-DRIFT-OR-PROCESS')
                for stage in ('tir', 'emit-qbe'):
                    observed = row[stage] = invoke([candidate, stage, str(source)], args.timeout)
                    row['failures'].extend(stage_failures(case, observed, stage))
                internal = row['internal'] = invoke([str(probe), str(source)], args.timeout)
                if not internal['process_ok'] or internal.get('status') != 0:
                    row['failures'].append('INTERNAL-PROCESS')
                else:
                    try:
                        counts = row['counts'] = json.loads(internal['stdout'])
                        if not isinstance(counts, dict):
                            raise ValueError('counts must be an object')
                        row['failures'].extend(internal_failures(case, counts, internal['primaries']))
                    except (ValueError, TypeError):
                        row['failures'].append('INTERNAL-INVALID-JSON')
                row['outcome'] = 'FAIL' if row['failures'] else 'PASS-' + row['classification']
                report['records'].append(row)
                print(row['outcome'] + ': ' + case['file'], flush=True)
            if sha(probe) != report['probe_binary_sha256']:
                raise ValueError('probe binary changed during run')
        frozen_strict()
        load_cases()
        if (sha(FIXTURES / 'manifest.json') != manifest_hash or sha(FIXTURES / 'publication_probe.c') != probe_hash
                or source_hashes() != before or (overlay and sha(overlay) != overlay_hash)):
            raise ValueError('source/manifest/probe changed during run')
        for key, path in binaries.items():
            report['binaries'][key]['sha256_after'] = sha(Path(path))
            if report['binaries'][key]['sha256'] != report['binaries'][key]['sha256_after']:
                raise ValueError(key + ' binary changed during run')
        return finish(int(any(r['failures'] for r in report['records'])))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report['infrastructure_error'] = str(error)
        print('INFRASTRUCTURE: ' + str(error), flush=True)
        return finish(2)


if __name__ == '__main__':
    raise SystemExit(main())
