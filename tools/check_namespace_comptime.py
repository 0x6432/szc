#!/usr/bin/env python3
"""Independent namespace-comptime acceptance contract (no forced wrappers).

  ZIG=/path/to/zig python3 tools/check_namespace_comptime.py /path/to/szc --report result.json
  python3 tools/check_namespace_comptime.py --reference-only --stress --report reference.json

Reference must actually report version 0.17.0. Candidate is exercised at its
public sema-only `tir` stage, not through a demanded constant wrapper. Source
fixtures live outside autodiscovered golden directories. All child processes,
including version queries, use compiler_process.run's process-group timeout
cleanup. Reference caches and generated stress sources are private per run.

NYI never counts as language-error parity. The unsupported/resource lanes require
reference acceptance and an explicit matching implementation boundary; they are
reported separately from parity. Gate cases compare rejection classification,
not exact diagnostic text, and remain visibly distinct in the report.
"""
import argparse
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
from collections import Counter
from compiler_process import run

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / 'tests' / 'namespace_comptime_contract'
PIN = '0.17.0'
NYI = 'not supported by szc yet:'
PRIMARY = re.compile(r'^(?:.*?:\d+:\d+: )?error: (.*)$', re.M)


def finite_positive(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('must be finite and greater than zero')
    return value


def compiler_path(text):
    found = shutil.which(text)
    return str(Path(found or text).resolve())


def invoke(command, timeout):
    before = time.monotonic()
    row = {'command': command}
    try:
        completed = run(command, timeout=timeout)
        row.update(status=completed.returncode,
                   stdout=completed.stdout.decode(errors='replace'),
                   stderr=completed.stderr.decode(errors='replace'))
    except subprocess.TimeoutExpired as error:
        row.update(timeout=True, stdout=(error.output or b'').decode(errors='replace'),
                   stderr=(error.stderr or b'').decode(errors='replace'))
    except OSError as error:
        row.update(os_error=True, stdout='', stderr=str(error))
    row['seconds'] = round(time.monotonic() - before, 3)
    row['primaries'] = PRIMARY.findall(row['stderr'])
    row['nyi'] = NYI in row['stderr']
    row['process_ok'] = not (row.get('timeout') or row.get('os_error')) and row.get('status') in (0, 1)
    return row


def fixture(path):
    source = path.read_text()
    metadata = dict(re.findall(r'^// nc-([\w-]+): (.*)$', source, re.M))
    lane = metadata.get('lane')
    if lane not in ('accept', 'reject', 'unsupported', 'gate', 'resource'):
        raise ValueError(f'{path}: invalid/missing nc-lane')
    if lane == 'reject' and 'primary' not in metadata:
        raise ValueError(f'{path}: exact rejection lane requires nc-primary')
    if lane in ('unsupported', 'resource') and 'nyi' not in metadata:
        raise ValueError(f'{path}: boundary lane requires nc-nyi')
    return {'label': path.name, 'path': path, 'source': source, 'metadata': metadata}


def stress_cases(directory):
    """Small fixed limits: agreed 128-deep boundary, and cumulative work stress/control.

    No loop/call/mutable comptime work can mask scheduling. The 300-link chain
    demands actual ordinary constant dependencies from an anonymous block. This
    is a proposed bounded-scope admission test, NOT a Zig language rejection.
    Wide control is intentionally below proposed million-unit work limits.
    """
    for n, lane in ((32, 'accept'), (300, 'resource')):
        lines = [f'// nc-lane: {lane}']
        if lane == 'resource':
            lines.append('// nc-nyi: depth')
        lines.append('comptime { if(d0!=%d) @panic("depth result"); }' % n)
        lines.extend(f'const d{i}:u32=d{i+1}+1;' for i in range(n))
        lines += [f'const d{n}:u32=0;', 'pub fn main()void{}']
        path = directory / f'resource_depth_{n}.zig'
        path.write_text('\n'.join(lines) + '\n')
        yield fixture(path)
    path = directory / 'resource_wide_control.zig'
    path.write_text('// nc-lane: accept\n' +
                    '\n'.join('comptime {const x:u8=3+4;if(x!=7) @panic("work control");}'
                              for _ in range(2048)) + '\npub fn main()void{}\n')
    yield fixture(path)
    # Each addition lowers to two integer literals and an add: >1,048,576
    # executed IR instructions cumulatively, with only 96 additions per block.
    # No per-block depth, loop, call, switch-label or array budget can mask it.
    path = directory / 'resource_work_limit.zig'
    block = 'comptime {' + '_=1+2;' * 96 + '}\n'
    path.write_text('// nc-lane: resource\n// nc-nyi: work\n' + block * 4096 +
                    'pub fn main()void{}\n')
    yield fixture(path)


def expected_primary(case):
    return case['metadata'].get('primary', '').replace('{module}', case['path'].stem)


def reference_check(case, result):
    if not result['process_ok']:
        return 'REFERENCE-PROCESS-FAIL'
    negative = case['metadata']['lane'] in ('reject', 'gate')
    if result['status'] != int(negative):
        return 'REFERENCE-EXPECTATION-FAIL'
    if negative and not result['primaries']:
        return 'REFERENCE-DIAGNOSTIC-MISSING'
    if case['metadata']['lane'] == 'reject' and result['primaries'] != [expected_primary(case)]:
        return 'REFERENCE-PRIMARY-MISMATCH'
    if 'errors' in case['metadata'] and len(result['primaries']) != int(case['metadata']['errors']):
        return 'REFERENCE-ERROR-COUNT-MISMATCH'
    return None


def candidate_check(case, result):
    lane = case['metadata']['lane']
    if not result['process_ok']:
        return 'FAIL-CANDIDATE-PROCESS'
    if result['status'] == 0 and (result['primaries'] or result['nyi']):
        return 'FAIL-CANDIDATE-SUCCESS-WITH-DIAGNOSTIC'
    if lane == 'accept':
        return 'PARITY-ACCEPT' if result['status'] == 0 else ('FAIL-SUPPORTED-NYI' if result['nyi'] else 'FAIL-SUPPORTED-REJECT')
    if lane in ('unsupported', 'resource'):
        if result['status'] == 0:
            return 'FAIL-BOUNDARY-SILENT-ACCEPT'
        if not result['nyi']:
            return 'FAIL-BOUNDARY-NOT-EXPLICIT-NYI'
        required = case['metadata']['nyi']
        if not any(NYI in line and required in line for line in result['stderr'].splitlines()):
            return 'FAIL-BOUNDARY-WRONG-NYI'
        return 'VALID-UNSUPPORTED' if lane == 'unsupported' else 'RESOURCE-LIMITED'
    if result['status'] == 0:
        return 'FAIL-SILENT-ACCEPT'
    if result['nyi']:
        return 'FAIL-INVALID-REFERENCE-NYI'
    if not result['primaries']:
        return 'FAIL-CANDIDATE-DIAGNOSTIC-MISSING'
    if lane == 'gate':
        return 'GATE-REJECT-TEXT-NOT-ASSERTED'
    if result['primaries'] != [expected_primary(case)]:
        return 'FAIL-PRIMARY-MISMATCH'
    if 'errors' in case['metadata'] and len(result['primaries']) != int(case['metadata']['errors']):
        return 'FAIL-ERROR-COUNT-MISMATCH'
    return 'PARITY-REJECT'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('candidate', nargs='?', help='candidate executable (required except --reference-only)')
    parser.add_argument('--zig', default=os.environ.get('ZIG', 'zig'), help='reference executable (default: ZIG or PATH zig)')
    parser.add_argument('--reference-only', action='store_true')
    parser.add_argument('--timeout', type=finite_positive, default=20)
    parser.add_argument('--stress', action='store_true', help='include bounded depth/work stress and corresponding controls')
    parser.add_argument('--select', action='append', default=[], help='case-name substring (repeatable)')
    parser.add_argument('--report', type=Path, help='JSON report path')
    args = parser.parse_args()
    if not args.reference_only and not args.candidate:
        parser.error('candidate is required unless --reference-only')
    zig = compiler_path(args.zig)
    candidate = compiler_path(args.candidate) if args.candidate else None
    report = {'title': 'bounded namespace comptime independent acceptance', 'reference_only': args.reference_only,
              'reference_pin': PIN, 'candidate_stage': 'tir', 'records': [],
              'scope_note': 'positive accepts alone are vacuous without paired execution-marker rejects; no forced evaluator wrappers',
              'resource_policy': {'dependency_depth_boundary': 128, 'stress_depth': 300, 'wide_control_blocks': 2048, 'work_stress_blocks': 4096,
                                  'work_additions_per_block': 96, 'shared_work_boundary': 1048576}}

    def finish(code):
        report['summary'] = dict(Counter(row['outcome'] for row in report['records']))
        report['exit_code'] = code
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report['summary'], sort_keys=True))
        return code

    report['version'] = invoke([zig, 'version'], args.timeout)
    if not report['version']['process_ok'] or report['version'].get('status') != 0 or report['version']['stdout'].strip() != PIN:
        report['infrastructure_error'] = 'reference must actually report version 0.17.0'
        print(report['infrastructure_error'])
        return finish(2)
    if candidate:
        try:
            report['candidate_sha256_before'] = hashlib.sha256(Path(candidate).read_bytes()).hexdigest()
        except OSError as error:
            report['infrastructure_error'] = str(error)
            return finish(2)
    with tempfile.TemporaryDirectory(prefix='szc-namespace-comptime-') as temporary:
        private = Path(temporary)
        local_cache = private / 'local-cache'
        global_cache = private / 'global-cache'
        generated = private / 'generated'
        generated.mkdir()
        selected = [fixture(path) for path in sorted(FIXTURES.glob('*.zig'))]
        if args.stress:
            selected.extend(stress_cases(generated))
        if args.select:
            selected = [case for case in selected if any(token in case['label'] for token in args.select)]
        if not selected:
            report['infrastructure_error'] = 'no matching cases'
            return finish(2)
        for case in selected:
            path = case['path']
            row = {'label': case['label'], 'lane': case['metadata']['lane'],
                   'metadata': case['metadata'], 'source': case['source'],
                   'source_sha256': hashlib.sha256(case['source'].encode()).hexdigest()}
            row['reference'] = invoke([zig, 'build-exe', str(path), '-j2', '-fno-emit-bin',
                                       '--cache-dir', str(local_cache), '--global-cache-dir', str(global_cache)], args.timeout)
            issue = reference_check(case, row['reference'])
            if not args.reference_only:
                # Run even when reference expectation fails: confounded gates remain visible.
                row['candidate'] = invoke([candidate, 'tir', str(path)], args.timeout)
            row['outcome'] = issue or ('REFERENCE-VALIDATED' if args.reference_only else candidate_check(case, row['candidate']))
            report['records'].append(row)
            print(f"{row['outcome']}: {case['label']}", flush=True)
    if candidate:
        report['candidate_sha256_after'] = hashlib.sha256(Path(candidate).read_bytes()).hexdigest()
        if report['candidate_sha256_before'] != report['candidate_sha256_after']:
            report['infrastructure_error'] = 'candidate executable changed during run'
            return finish(2)
    failed = any(row['outcome'].startswith(('FAIL-', 'REFERENCE-')) and row['outcome'] != 'REFERENCE-VALIDATED'
                 for row in report['records'])
    return finish(int(failed))


if __name__ == '__main__':
    raise SystemExit(main())
