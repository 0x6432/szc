#!/usr/bin/env python3
"""Compile-only pinned finite explicit error-set type-identity foundation contract.

No fixture is executed: accepted error-set programs are compile-only observations, never native evidence. Reference uses build-obj
-fno-emit-bin; candidate uses tir. All 30 cases remain visible with exact observed
primary lists. Outside-foundation value/layout/ABI/use NYI gates stay strict failures, never
language parity credit. No source wrappers, rewrites or failure filters.
Uses compiler_process.run for finite execution+cleanup; caches are private.
Discover compilers by explicit path, ZIG/SZC, local build paths or PATH.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from compiler_process import run

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests' / 'error_set_type_contract_20261019'
PRIMARY = re.compile(r'^(?:.*?:\d+:\d+: )?error: (.*)$', re.M)
NYI = 'not supported by szc yet:'


def positive(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return value


def discover(explicit, env, names):
    if explicit or os.environ.get(env):
        choices = [explicit or os.environ[env]]
    else:
        choices = names
    for name in choices:
        found = shutil.which(str(name))
        path = Path(found or name).expanduser().resolve()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
    raise ValueError(f'{env}: no executable found; supply explicit path or {env}')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def invoke(command, timeout):
    before = time.monotonic()
    row = {'command': command}
    try:
        result = run(command, timeout=timeout)
        row.update(status=result.returncode, stdout=result.stdout.decode(errors='replace'),
                   stderr=result.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as error:
        row.update(timeout=True, stdout=(error.output or b'').decode(errors='replace'),
                   stderr=(error.stderr or b'').decode(errors='replace'),
                   cleanup={'capture_incomplete': getattr(error, 'cleanup_capture_incomplete', False),
                            'reap_incomplete': getattr(error, 'cleanup_reap_incomplete', False),
                            'errors': list(getattr(error, 'cleanup_errors', ()))})
    except OSError as error:
        row.update(os_error=True, stdout=getattr(error, 'cleanup_stdout', b'').decode(errors='replace'),
                   stderr=getattr(error, 'cleanup_stderr', str(error).encode()).decode(errors='replace'), os_error_detail=str(error))
    row['seconds'] = round(time.monotonic() - before, 4)
    row['process_ok'] = not (row.get('timeout') or row.get('os_error')) and row.get('status') in (0, 1)
    row['primaries'] = PRIMARY.findall(row['stderr'])
    row['nyi'] = NYI in row['stderr']
    return row


def check(case, result, reference=False):
    if not result['process_ok']:
        return 'FAIL-REFERENCE-PROCESS' if reference else 'FAIL-CANDIDATE-PROCESS'
    expected = case['reference']
    if result['status'] != expected['status']:
        return 'FAIL-REFERENCE-STATUS' if reference else 'FAIL-PARITY-STATUS'
    if result['nyi']:
        return 'FAIL-UNEXPECTED-NYI'
    if result['primaries'] != expected['primaries']:
        return 'FAIL-REFERENCE-PRIMARY' if reference else 'FAIL-PARITY-PRIMARY'
    return 'REFERENCE-VALIDATED' if reference else ('PARITY-REJECT' if result['status'] else 'PARITY-ACCEPT')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('candidate', nargs='?')
    parser.add_argument('--zig')
    parser.add_argument('--reference-only', action='store_true')
    parser.add_argument('--timeout', type=positive, default=20)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = {'contract': 'error_set_types_20261019', 'stage': 'tir', 'reference_pin': '0.17.0',
              'reference_only': args.reference_only, 'records': [], 'baseline_commit': 'ea86b15887c33c6850123e82f6590ad5059b0f74',
              'scope': 'Compile-only, NEVER execute error-set fixtures. Exact primary lists, not notes/caret/native parity; no wrappers, skips or failure filtering. Earlier gates stay unresolved, not parity credit.'}

    def finish(code):
        report['summary'] = dict(Counter(row['outcome'] for row in report['records']))
        report['exit_code'] = code
        report['categories'] = {category: dict(Counter(row['outcome'] for row in report['records'] if row.get('category') == category))
                                for category in sorted({row['category'] for row in report['records']})}
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], sort_keys=True), flush=True)
        return code

    try:
        manifest = json.loads((FIXTURES / 'manifest.json').read_text())
        cases = manifest['cases']
        actual = {p.name for p in FIXTURES.glob('*.zig')}
        if not cases or len(cases) > 30 or actual != {c['file'] for c in cases} or len(actual) != len(cases):
            raise ValueError('fixture manifest must cover every .zig exactly once')
        for case in cases:
            if Path(case['file']).name != case['file'] or case['lane'] != 'parity':
                raise ValueError('invalid case')
            expected = case['reference']
            if expected['status'] not in (0, 1) or bool(expected['primaries']) != bool(expected['status']):
                raise ValueError('expected primary list must agree with reference status')
        zig = discover(args.zig, 'ZIG', ['zig'])
        candidate = None if args.reference_only else discover(args.candidate, 'SZC', [ROOT/'build/szc', ROOT/'build/asan/szc', 'szc'])
        report['version'] = invoke([zig, 'version'], args.timeout)
        if not report['version']['process_ok'] or report['version'].get('status') != 0 or report['version']['stdout'].strip() != '0.17.0' or manifest['reference_version'] != '0.17.0':
            raise ValueError('reference must actually report version 0.17.0')
        report['candidate_sha256_before'] = sha(Path(candidate)) if candidate else None
        report['reference_sha256'] = sha(Path(zig))
        with tempfile.TemporaryDirectory(prefix='szc-error-set-type-20261019-') as temp:
            for case in cases:
                path = FIXTURES / case['file']
                if sha(path) != case['source_sha256']:
                    raise ValueError(f"frozen source changed: {case['file']}")
                row = {'file': case['file'], 'lane': case['lane'], 'source': path.read_text(), 'source_sha256': sha(path), 'observation': case.get('observation'), 'category': case.get('category')}
                row['reference'] = invoke([zig, 'build-obj', str(path), '-j2', '-fno-emit-bin',
                                           '--cache-dir', str(Path(temp)/'local'), '--global-cache-dir', str(Path(temp)/'global')], args.timeout)
                ref_outcome = check(case, row['reference'], reference=True)
                if candidate:
                    row['candidate'] = invoke([candidate, 'tir', str(path)], args.timeout)
                row['outcome'] = ref_outcome if ref_outcome != 'REFERENCE-VALIDATED' or not candidate else check(case, row['candidate'])
                report['records'].append(row)
                print(f"{row['outcome']}: {case['file']}", flush=True)
        report['candidate_sha256_after'] = sha(Path(candidate)) if candidate else None
        if report['candidate_sha256_after'] != report['candidate_sha256_before']:
            raise ValueError('candidate executable changed during run')
    except (OSError, ValueError, KeyError) as error:
        report['infrastructure_error'] = str(error)
        print(f'INFRASTRUCTURE: {error}', flush=True)
        return finish(2)
    return finish(int(any(r['outcome'].startswith('FAIL-') for r in report['records'])))


if __name__ == '__main__':
    raise SystemExit(main())
