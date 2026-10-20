#!/usr/bin/env python3
"""Two separately authorized vararg containment regressions; NEVER execute fixtures.

Default candidate-only: no Zig discovery. Explicit --zig revalidates actual0.17
reference status/primaries. Both tir and emit-qbe are inspected: refusal must
contain no sink call or invalid NONE reference, while ordinary c_int retains its
valid call. Empty QBE alone is NOT containment proof; static review/full gates
are still independent. The error witness is reference-CABI-invalid, not parity.
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
FIXTURES = ROOT / 'tests' / 'error_family_varargs_20261019'
PRIMARY = re.compile(r'^(?:.*?:\d+:\d+: )?error: (.*)$', re.M)
LABELS = {'REF-INVALID-NYI-REFUSAL', 'ACCEPT'}


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
    row['sanitizer_finding'] = any(text in row['stderr'] for text in
        ['ERROR: AddressSanitizer', 'AddressSanitizer:DEADLYSIGNAL', 'ERROR: LeakSanitizer',
         'UndefinedBehaviorSanitizer', 'runtime error:'])
    row['process_ok'] = not (row.get('timeout') or row.get('os_error') or row['sanitizer_finding']) and row.get('status') in (0, 1)
    row['primaries'] = PRIMARY.findall(row['stderr'])
    return row


TIR_SINK_CALL = re.compile(r"\bcall\s+sink\s*\(")
QBE_SINK_CALL = re.compile(r"\bcall\s+\$sink\s*\(")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('candidate', nargs='?')
    parser.add_argument('--zig', help='Optional exact0.17.0 revalidation; omitted means NO Zig lookup')
    parser.add_argument('--timeout', type=positive, default=20)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = {'contract':'error_family_varargs_20261019', 'runner_sha256':sha(Path(__file__)),
              'reference_requested':bool(args.zig), 'records':[],
              'scope':'Two separately authorized appendix sources, not strict60/durable26 members. NYI is not language parity; TIR call/NONE containment asserted explicitly, no native execution.'}

    def finish(code):
        report['summary'] = dict(Counter(r['outcome'] for r in report['records']))
        report['containment_contract_exit'] = code
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report['summary'], sort_keys=True), flush=True)
        return code

    try:
        manifest = json.loads((FIXTURES/'manifest.json').read_text())
        report['manifest_sha256'] = sha(FIXTURES/'manifest.json')
        if manifest['state'] != 'FROZEN-APPROVED-POLICY' or manifest['reference_version'] != '0.17.0':
            raise ValueError('vararg containment expectations not frozen/approved')
        cases = manifest['cases']
        if len(cases)!=2 or {p.name for p in FIXTURES.glob('*.zig')} != {c['file'] for c in cases}:
            raise ValueError('must preserve exactly two authorized appendix programs')
        if Counter(c['label'] for c in cases) != Counter({'ACCEPT':1,'REF-INVALID-NYI-REFUSAL':1}):
            raise ValueError('unexpected appendix labels')
        for case in cases:
            if Path(case['file']).name != case['file'] or sha(FIXTURES/case['file']) != case['source_sha256']:
                raise ValueError('authorized appendix source changed')
        candidate = discover(args.candidate,'SZC',[ROOT/'build/szc',ROOT/'build/asan/szc','szc'])
        report['candidate_sha256_before'] = sha(Path(candidate))
        zig = discover(args.zig,'ZIG',[]) if args.zig else None
        if zig:
            report['reference_version'] = invoke([zig,'version'],args.timeout)
            version = report['reference_version']
            if not version['process_ok'] or version['status']!=0 or version['stdout'].strip()!='0.17.0':
                raise ValueError('optional reference must actually report0.17.0')
            report['reference_sha256_before'] = sha(Path(zig))
        with tempfile.TemporaryDirectory(prefix='szc-error-family-vararg2-') as temp:
            for case in cases:
                path = FIXTURES/case['file']
                row = {'file':case['file'],'label':case['label'],'source':path.read_text(),
                       'source_sha256':sha(path),'expected_candidate':case['candidate'],
                       'expected_reference':case['reference'],'failures':[]}
                if zig:
                    row['reference'] = invoke([zig,'build-obj',str(path),'-j2','-fno-emit-bin',
                                               '--cache-dir',temp+'/local','--global-cache-dir',temp+'/global'],args.timeout)
                    ref = row['reference']
                    if not ref['process_ok']:
                        row['failures'].append('FAIL-REFERENCE-PROCESS')
                    elif ref['status']!=case['reference']['status'] or ref['primaries']!=case['reference']['primaries']:
                        row['failures'].append('FAIL-REFERENCE-STATUS-PRIMARY')
                expected = case['candidate']
                for stage in ['tir','emit-qbe']:
                    observed = row[stage] = invoke([candidate,stage,str(path)],args.timeout)
                    observed['sanitizer_finding'] = any(s in observed['stderr'] for s in
                        ['ERROR: AddressSanitizer','AddressSanitizer:DEADLYSIGNAL','ERROR: LeakSanitizer','UndefinedBehaviorSanitizer','runtime error:'])
                    if not observed['process_ok'] or observed['sanitizer_finding']:
                        row['failures'].append('FAIL-'+stage.upper()+'-PROCESS')
                        continue
                    if observed['status']!=expected['status'] or observed['primaries']!=expected['primaries']:
                        row['failures'].append('FAIL-'+stage.upper()+'-INTENDED-STATUS-PRIMARY')
                    if stage=='tir':
                        observed['sink_call_present'] = bool(TIR_SINK_CALL.search(observed['stdout']))
                        observed['invalid_none_reference'] = '%4294967295' in observed['stdout']
                        if observed['invalid_none_reference']:
                            row['failures'].append('FAIL-TIR-INVALID-NONE')
                        if observed['sink_call_present'] != expected['sink_call_present']:
                            row['failures'].append('FAIL-TIR-CALL-CONTAINMENT')
                    else:
                        observed['sink_call_present'] = bool(QBE_SINK_CALL.search(observed['stdout']))
                        if expected['status']==1 and observed['stdout']!='':
                            row['failures'].append('FAIL-GENERATED-OUTPUT-ON-REFUSAL')
                        if expected['status']==0 and (not observed['stdout'] or not observed['sink_call_present']):
                            row['failures'].append('FAIL-ORDINARY-QBE-CALL-MISSING')
                row['outcome'] = row['failures'][0] if row['failures'] else 'PASS-'+case['label']
                report['records'].append(row)
                print(row['outcome']+': '+case['file'],flush=True)
        report['candidate_sha256_after'] = sha(Path(candidate))
        if report['candidate_sha256_after']!=report['candidate_sha256_before']:
            raise ValueError('candidate changed during validation')
        if zig:
            report['reference_sha256_after'] = sha(Path(zig))
            if report['reference_sha256_after']!=report['reference_sha256_before']:
                raise ValueError('reference changed during validation')
        for case in cases:
            if sha(FIXTURES/case['file'])!=case['source_sha256']:
                raise ValueError('appendix source changed during validation')
    except (OSError,ValueError,KeyError) as error:
        report['infrastructure_error'] = str(error)
        print('INFRASTRUCTURE: '+str(error),flush=True)
        return finish(2)
    return finish(int(any(r['failures'] for r in report['records'])))


if __name__=='__main__':
    raise SystemExit(main())
