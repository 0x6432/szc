#!/usr/bin/env python3
"""Twelve durable finite-error-set type boundary checks; NEVER execute.

Candidate-only by default: no Zig discovery, invocation or dependency. Optional
--zig revalidates frozen Zig0.17.0 reference status/primaries in private caches.
Candidate emit-qbe must refuse nine representation cases and one eager duplicate with exact intended primary lists and
EMPTY generated stdout. Canonical type identity and ordinary concat must still generate.
These are policy-fence checks, NOT nine error-set language-parity passes. In
particular reference-invalid undefined concat and error-set C ABI gets NYI refusal, not diagnostic
parity. No source wrappers, filters, native fixture execution or external QBE needed.
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
FIXTURES = ROOT / 'tests' / 'error_set_type_boundaries_20261019'
PRIMARY = re.compile(r'^(?:.*?:\d+:\d+: )?error: (.*)$', re.M)
LABELS = {'VALID-UNSUPPORTED-FENCE', 'REF-INVALID-NYI-REFUSAL', 'ACCEPT', 'PARITY-REJECT'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def positive(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return value


def discover(explicit, env, defaults):
    choices = [explicit or os.environ[env]] if explicit or os.environ.get(env) else defaults
    for item in choices:
        found = shutil.which(str(item))
        path = Path(found or item).expanduser().resolve()
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
    raise ValueError(f'{env}: supply an executable explicitly or through {env}')


def invoke(command, timeout):
    before = time.monotonic()
    row = {'command': command}
    try:
        result = run(command, timeout=timeout)
        row.update(status=result.returncode, stdout=result.stdout.decode(errors='replace'),
                   stderr=result.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as error:
        row.update(timeout=True, stdout=(error.output or b'').decode(errors='replace'),
                   stderr=(error.stderr or b'').decode(errors='replace'), cleanup={
                       'capture_incomplete': getattr(error, 'cleanup_capture_incomplete', False),
                       'reap_incomplete': getattr(error, 'cleanup_reap_incomplete', False),
                       'errors': list(getattr(error, 'cleanup_errors', ()))})
    except OSError as error:
        row.update(os_error=True, stdout=getattr(error, 'cleanup_stdout', b'').decode(errors='replace'),
                   stderr=getattr(error, 'cleanup_stderr', str(error).encode()).decode(errors='replace'),
                   os_error_detail=str(error))
    row['seconds'] = round(time.monotonic() - before, 4)
    row['process_ok'] = not (row.get('timeout') or row.get('os_error')) and row.get('status') in (0, 1)
    row['primaries'] = PRIMARY.findall(row['stderr'])
    return row


def candidate_failure(case, row):
    if not row['process_ok']:
        return 'FAIL-CANDIDATE-PROCESS'
    expected = case['candidate']
    if row['status'] != expected['status']:
        return 'FAIL-CANDIDATE-STATUS'
    if row['primaries'] != expected['primaries']:
        return 'FAIL-INTENDED-PRIMARY'
    if expected['generated_stdout'] == 'empty' and row['stdout'] != '':
        return 'FAIL-GENERATED-OUTPUT-ON-REFUSAL'
    if expected['generated_stdout'] == 'nonempty' and not row['stdout']:
        return 'FAIL-MISSING-ORDINARY-GENERATED-OUTPUT'
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('candidate', nargs='?')
    parser.add_argument('--zig', help='Optional exact0.17.0 reference revalidation; omitted means NO Zig dependency')
    parser.add_argument('--timeout', type=positive, default=20)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = {'contract': 'error_set_type_boundaries_20261019', 'stage': 'emit-qbe',
              'reference_requested': bool(args.zig), 'records': [],
              'scope': 'Nine intended fail-closed representation refusals + eager duplicate rejection + two accepts. NYI is not language parity. Never execute fixtures.'}

    def finish(code):
        report['summary'] = dict(Counter(x['outcome'] for x in report['records']))
        report['policy_contract_exit'] = code
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], sort_keys=True), flush=True)
        return code

    try:
        manifest = json.loads((FIXTURES / 'manifest.json').read_text())
        cases = manifest['cases']
        actual = {p.name for p in FIXTURES.glob('*.zig')}
        if len(cases) != 12 or len(actual) != 12 or actual != {x['file'] for x in cases}:
            raise ValueError('manifest must cover exactly twelve unique source files')
        if Counter(x['label'] for x in cases) != Counter({'VALID-UNSUPPORTED-FENCE': 6, 'REF-INVALID-NYI-REFUSAL': 3, 'ACCEPT': 2, 'PARITY-REJECT': 1}):
            raise ValueError('unexpected contract label distribution')
        if manifest['reference_version'] != '0.17.0' or manifest['candidate_stage'] != 'emit-qbe':
            raise ValueError('contract version/stage changed')
        for case in cases:
            if Path(case['file']).name != case['file'] or case['label'] not in LABELS:
                raise ValueError('invalid fixture path/label')
            if sha(FIXTURES / case['file']) != case['source_sha256']:
                raise ValueError(f"source changed: {case['file']}")
            if case['candidate']['status'] not in (0, 1):
                raise ValueError('invalid candidate status')
        candidate = discover(args.candidate, 'SZC', [ROOT/'build/szc', ROOT/'build/asan/szc', 'szc'])
        report['candidate_sha256_before'] = sha(Path(candidate))
        zig = None
        if args.zig:
            zig = discover(args.zig, 'ZIG', [])
            report['reference_version'] = invoke([zig, 'version'], args.timeout)
            version = report['reference_version']
            if not version['process_ok'] or version['status'] != 0 or version['stdout'].strip() != '0.17.0':
                raise ValueError('optional reference must actually report version0.17.0')
            report['reference_sha256'] = sha(Path(zig))
        with tempfile.TemporaryDirectory(prefix='szc-error-set-boundary12-') as temp:
            for case in cases:
                path = FIXTURES / case['file']
                row = {'file': case['file'], 'label': case['label'], 'source_sha256': sha(path),
                       'expected_candidate': case['candidate'], 'expected_reference': case['reference']}
                ref_failure = None
                if zig:
                    row['reference'] = invoke([zig, 'build-obj', str(path), '-j2', '-fno-emit-bin',
                                               '--cache-dir', str(Path(temp)/'local'),
                                               '--global-cache-dir', str(Path(temp)/'global')], args.timeout)
                    ref = row['reference']
                    if not ref['process_ok']:
                        ref_failure = 'FAIL-REFERENCE-PROCESS'
                    elif ref['status'] != case['reference']['status'] or ref['primaries'] != case['reference']['primaries']:
                        ref_failure = 'FAIL-REFERENCE-STATUS-PRIMARY'
                row['candidate'] = invoke([candidate, 'emit-qbe', str(path)], args.timeout)
                failure = candidate_failure(case, row['candidate'])
                row['outcome'] = ref_failure or failure or 'PASS-' + case['label']
                report['records'].append(row)
                print(f"{row['outcome']}: {case['file']}", flush=True)
        report['candidate_sha256_after'] = sha(Path(candidate))
        if report['candidate_sha256_before'] != report['candidate_sha256_after']:
            raise ValueError('candidate changed during validation')
    except (OSError, ValueError, KeyError) as error:
        report['infrastructure_error'] = str(error)
        print(f'INFRASTRUCTURE: {error}', flush=True)
        return finish(2)
    return finish(int(any(x['outcome'].startswith('FAIL-') for x in report['records'])))


if __name__ == '__main__':
    raise SystemExit(main())
