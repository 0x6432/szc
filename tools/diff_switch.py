#!/usr/bin/env python3
"""Focused 24-family switch contract: reference, rejection, stage and native lanes.

Examples:
  ZIG=/exact/zig python3 tools/diff_switch.py /path/to/szc --report /tmp/switch.json
  python3 tools/diff_switch.py --reference-only --report /tmp/reference.json
  python3 tools/diff_switch.py /path/to/szc --stages --stress --expect-budget
  python3 tools/diff_switch.py /path/to/szc /path/to/zig  # compile-only adapter

The conventional two-positional CLI uses the repository compile-only adapter
protocol (120-second budgets, all 84 fixed contract sources). It does not run
native executables or independently verify the Zig version; use --zig for the
full pinned-version/native/byte-parity harness. Neither mode filters known
contract failures or interprets an unrelated rejection as language parity.

No golden files are generated. NYI on an intended-supported program is a failure
unless --allow-nyi is explicitly supplied (reported unresolved, never parity).
Negative tests require a real language error, never an NYI. Primary diagnostics
are compared after normalizing source/module paths; --classification-only opts
out of diagnostic text parity, visibly in the report. All subprocesses use the
project's whole-process-group timeout cleanup, including native executables.
"""
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from compiler_process import run, run_compiler

ROOT = Path(__file__).resolve().parents[1]
ZIG = 'zig'
QBE = 'qbe'
NYI = 'not supported by szc yet:'
SZC_MODE = 'emit-qbe'

@dataclass
class Case:
    label: str
    source: str
    lane: str
    families: list
    path: Path | None = None
    stage: str | None = None

def cases(stages=False):
    for folder, lane in [('run', 'native'), ('switch_contract/errors', 'error')]:
        for path in sorted((ROOT / 'tests' / folder).glob('switch*.zig')):
            source = path.read_text()
            match = re.search(r'^// switch-families: ([\d,]+)$', source, re.M)
            yield Case(str(path.relative_to(ROOT)), source, lane,
                       [int(x) for x in match[1].split(',')] if match else [], path)
    if stages:
        for stage in ('ir', 'tir', 'codegen'):
            for path in sorted((ROOT / 'tests' / 'switch_contract' / stage).glob('switch*.zig')):
                yield Case(str(path.relative_to(ROOT)), path.read_text(),
                           'stage', [], path,
                           {'ir': 'ir', 'tir': 'tir', 'codegen': 'emit-qbe'}[stage])

def output_case():
    return Case('native/output-bytes', r'''// exit: 23
// stdout-hex: 7377697463680a
// link-libc: true
extern fn write(fd:c_int,buffer:[*]const u8,length:usize)isize;
fn f(x:u8)u8 {switch(x){1=>{_=write(1,"switch\n",7);return 23;},else=>return 1}}
pub fn main()u8{return f(1);}
''', 'native', [1,22])

def budget_case(functions, items):
    # Every function is forced by a call from main. No runtime-wide operation,
    # giant range enumeration, generic/comptime calls, or per-switch item excess.
    declarations = []
    calls = []
    for f in range(functions):
        labels = ','.join(str(i) for i in range(items))
        declarations.append(f'fn s{f}(x:u16)u16{{return switch(x){{{labels}=>x,else=>0}};}}')
        calls.append(f'if(s{f}(x)!=x)return 1;')
    source = '\n'.join(declarations) + '\npub fn main()u8{var seed:u16=1;const p:*volatile u16=&seed;const x=p.*;' + ''.join(calls) + 'return 0;}\n'
    return Case(f'budget/{functions}x{items}', source, 'budget', [24])

def primary(stderr):
    text = stderr.decode(errors='replace')
    match = re.search(r'(?:^|\n).*?\berror: (.*)', text)
    if not match:
        return None
    # Official and candidate may qualify nominal types with the source stem.
    return re.sub(r"(?<=')[A-Za-z_][A-Za-z_0-9]*\.", '', match[1])

def classification(result):
    if result['timeout']:
        return 'timeout'
    if result['os_error']:
        return 'tool-error'
    if result['status'] == 0:
        return 'accepted'
    if result['status'] != 1:
        return 'crash-or-tool-error'
    return 'nyi' if NYI in result['stderr'] else 'language-error'

def invoke(command, timeout):
    started = time.monotonic()
    record = dict(command=[str(x) for x in command], timeout=False, os_error=False)
    try:
        result = run(record['command'], timeout=timeout)
        status, out, err = result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        status, out, err = None, e.output or b'', e.stderr or b''
        record['timeout'] = True
    except OSError as e:
        status, out, err = None, b'', str(e).encode()
        record['os_error'] = True
    record.update(status=status, stdout=out.decode(errors='replace'),
                  stderr=err.decode(errors='replace'), stdout_hex=out.hex(),
                  stderr_hex=err.hex(), seconds=round(time.monotonic()-started, 3),
                  primary=primary(err))
    record['classification'] = classification(record)
    return record

def positive_number(text):
    number = float(text)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return number

def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate', nargs='?')
    p.add_argument('reference', nargs='?',
                   help='conventional compile-only adapter; use --zig for full native checks')
    p.add_argument('--zig', default=os.environ.get('ZIG', ZIG))
    p.add_argument('--qbe', default=os.environ.get('SZC_QBE', QBE))
    p.add_argument('--reference-only', action='store_true')
    p.add_argument('--report', type=Path)
    p.add_argument('--select', action='append', default=[])
    p.add_argument('--timeout', type=positive_number, default=30)
    p.add_argument('--run-timeout', type=positive_number, default=5)
    p.add_argument('--allow-nyi', action='store_true')
    p.add_argument('--classification-only', action='store_true')
    p.add_argument('--stages', action='store_true')
    p.add_argument('--stress', action='store_true')
    p.add_argument('--stress-functions', type=int, default=256)
    p.add_argument('--stress-items', type=int, default=512)
    p.add_argument('--expect-budget', action='store_true')
    return p

def resolve_executable(value):
    if '/' in value:
        return str(Path(value).expanduser().resolve())
    found = shutil.which(value)
    if found is None:
        raise SystemExit(f'executable not found on PATH: {value}')
    return found

def run_cases(args, selected, title='switch'):
    args.zig = resolve_executable(args.zig)
    if not args.reference_only:
        args.qbe = resolve_executable(args.qbe)
        if args.candidate: args.candidate = resolve_executable(args.candidate)
    report = dict(title=title, reference_only=args.reference_only,
                  diagnostic_parity=not args.classification_only,
                  allow_nyi=args.allow_nyi, records=[], failures=[], unresolved=[])
    if not args.reference_only and not args.candidate:
        raise SystemExit('candidate path required unless --reference-only')
    if args.expect_budget and (not args.stress or args.reference_only):
        raise SystemExit('--expect-budget requires a candidate and --stress')
    if not 1 <= args.stress_functions <= 4096 or not 1 <= args.stress_items <= 4096:
        raise SystemExit('stress dimensions must be in 1..4096')
    # Driver child processes inherit this exact backend selection.
    if not args.reference_only: os.environ['SZC_QBE'] = args.qbe
    if not args.reference_only:
        report['candidate_sha256_before'] = hashlib.sha256(Path(args.candidate).read_bytes()).hexdigest()
    report['version'] = invoke([args.zig, 'version'], args.timeout)
    if report['version']['status'] != 0 or report['version']['stdout'].strip() != '0.17.0':
        raise SystemExit('expected exact official Zig 0.17.0; see --zig/ZIG')
    with tempfile.TemporaryDirectory(prefix='szc-switch-') as tmp:
        tmp = Path(tmp)
        for index, case in enumerate(selected):
            directory = tmp / str(index)
            directory.mkdir()
            source = case.path
            if source is None:
                source = directory / 'switch_stress.zig'
                source.write_text(case.source)
            row = dict(label=case.label, lane=case.lane, families=case.families,
                       source=case.source, outcome='pending',
                       source_scope='global-comptime' if re.search(r'(?m)^comptime\s*\{', case.source) else 'reached-body')
            report['records'].append(row)
            def fail(reason):
                row['outcome'] = 'FAIL'
                report['failures'].append(case.label+': '+reason)
            native = case.lane == 'native'
            refbin = directory / 'reference'
            cmd = [args.zig, 'build-exe' if native else 'build-obj', str(source), '-j2',
                   '--cache-dir', str(tmp/'cache-local'), '--global-cache-dir', str(tmp/'cache-global')]
            cmd += [f'-femit-bin={refbin}'] if native else ['-fno-emit-bin']
            if '// link-libc: true' in case.source: cmd.append('-lc')
            ref = row['reference_compile'] = invoke(cmd, args.timeout)
            want = 'language-error' if case.lane == 'error' else 'accepted'
            if ref['classification'] != want or (want == 'language-error' and not ref['primary']):
                fail('reference contract: '+ref['classification'])
            elif native:
                ref_run = row['reference_run'] = invoke([str(refbin)], args.run_timeout)
                expected = int(re.search(r'^// exit: (\d+)$', case.source, re.M)[1])
                output = re.search(r'^// stdout-hex: ([0-9a-f]*)$', case.source, re.M)
                expected_output = output[1] if output else ''
                if (ref_run['status'] != expected or ref_run['stdout_hex'] != expected_output or
                        ref_run['stderr_hex'] or ref_run['timeout']):
                    fail('reference native bytes/exit differ from fixture headers')
            if row['outcome'] != 'FAIL' and not args.reference_only:
                binary = directory/'candidate'
                cmd = [args.candidate, 'build-exe', str(source), '-o', str(binary), '--keep-temps'] if native else [args.candidate, case.stage or 'emit-qbe', str(source)]
                got = row['candidate_compile'] = invoke(cmd, args.timeout)
                kind = got['classification']
                if case.lane == 'nyi':
                    if kind != 'nyi': fail('valid language boundary requires explicit NYI; got '+kind)
                    else:
                        row['outcome'] = 'EXPECTED-NYI'
                        row['gate_diagnostic'] = got['primary']
                elif case.lane == 'error':
                    if kind != 'language-error' or not got['primary']:
                        fail('expected language error, got '+kind)
                        if kind == 'accepted' and row['source_scope'] == 'global-comptime':
                            row['outcome'] = 'KNOWN-GLOBAL-COMPTIME-GAP'
                        if kind == 'nyi':
                            row['outcome'] = 'EARLIER-FEATURE-GATE'
                            row['gate_diagnostic'] = got['primary']
                    elif not args.classification_only and got['primary'] != ref['primary']:
                        fail('primary diagnostic mismatch')
                elif case.lane == 'budget' and args.expect_budget:
                    if kind != 'nyi' or 'cumulative switch work limit' not in got['stderr']:
                        fail('expected switch-specific cumulative-budget NYI')
                elif kind == 'nyi' and args.allow_nyi:
                    row['outcome'] = 'UNRESOLVED-NYI'
                    report['unresolved'].append(case.label)
                elif kind != 'accepted':
                    fail('intended-supported source got '+kind)
                elif native:
                    got_run = row['candidate_run'] = invoke([str(binary)], args.run_timeout)
                    ref_run = row['reference_run']
                    if (got_run['timeout'] or got_run['os_error'] or
                        any(got_run[key] != ref_run[key] for key in ('status','stdout_hex','stderr_hex'))):
                        fail('runtime bytes/exit mismatch')
                if native and kind == 'accepted':
                    # Keep native build and standalone IL frontends independently covered.
                    il = row['candidate_il'] = invoke([args.candidate, 'emit-qbe', str(source)], args.timeout)
                    if il['classification'] != 'accepted':
                        fail('standalone IL emission failed')
                    else:
                        retained = binary.with_suffix('.ssa')
                        if not retained.exists() or retained.read_bytes().hex() != il['stdout_hex']:
                            fail('retained and standalone QBE bytes differ')
            if row['outcome'] == 'pending': row['outcome'] = 'PASS'
            print(f"{row['outcome']} {case.label}", flush=True)
            if args.report:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(report, indent=2)+'\n')
    if not args.reference_only:
        report['candidate_sha256_after'] = hashlib.sha256(Path(args.candidate).read_bytes()).hexdigest()
        if report['candidate_sha256_before'] != report['candidate_sha256_after']:
            report['failures'].append('candidate binary changed during run; results require rerun')
    report['summary'] = dict(cases=len(report['records']), failures=len(report['failures']),
                             unresolved=len(report['unresolved']),
                             outcomes={outcome: sum(r['outcome'] == outcome for r in report['records'])
                                       for outcome in sorted({r['outcome'] for r in report['records']})},
                             families=sorted({f for r in report['records'] for f in r['families']}))
    if args.report: args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report['summary']))
    return 1 if report['failures'] else 0

def compile_source(command, source, context, timeout=120):
    """Repository differential adapter; retain source/actor failure context."""
    return run_compiler(command, source, context, timeout=timeout)

def compile_differential(args, selected):
    """Two-positional compile-only protocol; no backend/native processes."""
    records, failures = [], []
    with tempfile.TemporaryDirectory(prefix='szc-switch-adapter-') as directory:
        path = Path(directory) / 'switch_contract.zig'
        for index, case in enumerate(selected, 1):
            path.write_text(case.source)
            candidate = None
            if not args.reference_only:
                candidate = compile_source([args.candidate, SZC_MODE, str(path)],
                    case.source, f'diff_switch case {index} szc', timeout=120)
            reference = compile_source([args.reference, 'build-exe', str(path),
                '-fno-emit-bin', '-j2'], case.source,
                f'diff_switch case {index} zig', timeout=120)
            row = dict(label=case.label, lane=case.lane, source=case.source,
                       reference_status=reference.returncode,
                       reference_stderr=reference.stderr.decode(errors='replace'))
            expected = 1 if case.lane == 'error' else 0
            if reference.returncode != expected or (expected and not primary(reference.stderr)):
                failures.append(case.label + ': reference contract mismatch')
            if candidate is not None:
                row.update(candidate_status=candidate.returncode,
                           candidate_stderr=candidate.stderr.decode(errors='replace'))
                if case.lane == 'nyi':
                    if candidate.returncode != 1 or NYI not in row['candidate_stderr']:
                        failures.append(case.label + ': expected explicit valid NYI')
                elif (candidate.returncode != reference.returncode or
                      primary(candidate.stderr) != primary(reference.stderr)):
                    failures.append(case.label + ': compile status/primary diagnostic mismatch')
            records.append(row)
    report = dict(mode='compile-only-adapter', version_verified=False,
                  native_executed=False, records=records, failures=failures,
                  summary=dict(cases=len(records), failures=len(failures)))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+'\n')
    for failure in failures:
        print('FAIL ' + failure, flush=True)
    print('switch compile-only adapter: ' + json.dumps(report['summary']), flush=True)
    return 1 if failures else 0

def main():
    p = parser()
    args = p.parse_args()
    selected = list(cases(args.stages or args.reference is not None))
    selected.append(output_case())
    if args.stress:
        control = budget_case(1, args.stress_items)
        control.label = f'budget-control/1x{args.stress_items}'
        control.lane = 'compile'
        selected.extend([control, budget_case(args.stress_functions, args.stress_items)])
    if args.select: selected = [c for c in selected if any(s in c.label for s in args.select)]
    if not selected: p.error('no matching cases')
    if args.reference is not None:
        if args.allow_nyi or args.classification_only or args.expect_budget:
            p.error('compile-only adapter does not permit parity/budget bypass flags; use --zig')
        return compile_differential(args, selected)
    return run_cases(args, selected)

if __name__ == '__main__':
    raise SystemExit(main())
