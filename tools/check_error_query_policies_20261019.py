#!/usr/bin/env python3
"""Candidate-only policy31 reuses canonical56 and exact authorized appendix57.

No Zig lookup by default, even when ZIG is set. Explicit --zig revalidates0.17.0.
Compile-only emit-qbe; no external QBE or fixture execution. Empty generated
output proves driver refusal only, not internal TIR/backing/call/store safety.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import tempfile
from check_error_queries_20261019 import sha, positive, discover, invoke

ROOT=Path(__file__).resolve().parents[1]
POLICY=ROOT/'tests/error_query_policies_20261019/manifest.json'
SOURCES=ROOT/'tests/error_query_contract_20261019'
APPENDIX=ROOT/'tests/error_query_typeof_20261019'
LABELS={'ACCEPT','PARITY-REJECT','STRUCTURAL-DIAGNOSTIC-REJECT','VALID-UNSUPPORTED-FENCE','REF-INVALID-NYI-REFUSAL'}

def check(expected,actual,generated=False):
    if not actual['process_ok']:return 'FAIL-PROCESS'
    if actual['status']!=expected['status']:return 'FAIL-STATUS'
    if actual['primaries']!=expected['primaries']:return 'FAIL-ORDERED-PRIMARIES'
    if generated and expected['status'] and actual['stdout']:return 'FAIL-REFUSAL-OUTPUT'
    if generated and not expected['status'] and not actual['stdout'].strip():return 'FAIL-ACCEPT-OUTPUT'
    return 'PASS'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate',nargs='?');p.add_argument('--zig');p.add_argument('--timeout',type=positive,default=20);p.add_argument('--report',type=Path)
    args=p.parse_args();report={'contract':'error_query_policies_20261019','stage':'emit-qbe','runner_sha256':sha(Path(__file__)),'helper_sha256':sha(ROOT/'tools/check_error_queries_20261019.py'),'reference_requested':bool(args.zig),'records':[],'scope':'Containment policies do not add language parity. Empty QBE is driver refusal only.'}
    def finish(code):
        report['exit_code']=code;report['summary']=dict(Counter(x['outcome'] for x in report['records']))
        if args.report:
            args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report['summary'],sort_keys=True),flush=True);return code
    try:
        m=json.loads(POLICY.read_text());sm=SOURCES/'manifest.json';am=APPENDIX/'manifest.json';strict=json.loads(sm.read_text());appendix=json.loads(am.read_text())
        if sha(sm)!=m['source_manifest_sha256'] or sha(am)!=m['appendix_manifest_sha256']:raise ValueError('frozen source metadata changed')
        if len(strict['cases'])!=56 or len(appendix['cases'])!=1:raise ValueError('source contracts must remain56plus1')
        by={('strict',c['file']):c for c in strict['cases']};by.update({('appendix',c['file']):c for c in appendix['cases']});cases=m['cases']
        if len(cases)!=31 or len({(c.get('source_group','strict'),c['file']) for c in cases})!=31 or sum(c.get('source_group','strict')=='appendix' for c in cases)!=1:raise ValueError('exactly30canonical plus1appendix required')
        for c in cases:
            group=c.get('source_group','strict');f=c['file'];key=(group,f)
            if Path(f).name!=f or key not in by or c['source_sha256']!=by[key]['source_sha256'] or c['reference']!=by[key]['reference']:raise ValueError('source/reference provenance changed')
            e=c['candidate']
            if c['label'] not in LABELS or e['status'] not in (0,1) or bool(e['primaries'])!=bool(e['status']) or (c['label']=='ACCEPT')!=(e['status']==0) or len(c['diagnostic_operations'])!=len(e['primaries']):raise ValueError('invalid typed ordered policy')
            if sha((APPENDIX if group=='appendix' else SOURCES)/f)!=c['source_sha256']:raise ValueError('frozen source changed')
        candidate=Path(discover(args.candidate,'SZC',[ROOT/'build/szc',ROOT/'build/asan/szc','szc']));report['candidate_path']=str(candidate);report['candidate_sha256_before']=sha(candidate);report['policy_manifest_sha256']=sha(POLICY);report['source_manifest_sha256']=sha(sm);report['appendix_manifest_sha256']=sha(am)
        zig=Path(discover(args.zig,'__ERROR_QUERY_EXPLICIT_REFERENCE_ONLY__',[])) if args.zig else None
        if zig:
            report['reference_sha256_before']=sha(zig);v=invoke([str(zig),'version'],args.timeout);report['reference_version']=v
            if not v['process_ok'] or v['status'] or v['stdout'].strip()!='0.17.0':raise ValueError('reference must actually report0.17.0')
        with tempfile.TemporaryDirectory(prefix='error-query-policy31-') as tmp:
            for c in cases:
                src=(APPENDIX if c.get('source_group','strict')=='appendix' else SOURCES)/c['file'];row={'file':c['file'],'source_group':c.get('source_group','strict'),'label':c['label'],'source':src.read_text(),'source_sha256':sha(src),'expected':c['candidate'],'diagnostic_operations':c['diagnostic_operations']}
                row['candidate']=invoke([str(candidate),'emit-qbe',str(src)],args.timeout);row['outcome']=check(c['candidate'],row['candidate'],True)
                if zig:
                    row['reference']=invoke([str(zig),'build-obj',str(src),'-j2','-fno-emit-bin','--cache-dir',str(Path(tmp)/'local'),'--global-cache-dir',str(Path(tmp)/'global')],args.timeout);rr=check(c['reference'],row['reference'])
                    if rr!='PASS':row['outcome']='REFERENCE-'+rr
                if sha(src)!=c['source_sha256']:raise ValueError('source changed during run')
                report['records'].append(row);print(row['outcome']+': '+c['file'],flush=True)
        report['candidate_sha256_after']=sha(candidate)
        if report['candidate_sha256_before']!=report['candidate_sha256_after']:raise ValueError('candidate binary changed')
        if zig:
            report['reference_sha256_after']=sha(zig)
            if report['reference_sha256_before']!=report['reference_sha256_after']:raise ValueError('reference binary changed')
        return finish(0 if all(x['outcome']=='PASS' for x in report['records']) else 1)
    except (OSError,ValueError,KeyError,json.JSONDecodeError) as e:
        report['infrastructure_error']=str(e);print('ERROR: '+str(e),flush=True);return finish(2)

if __name__=='__main__':
    raise SystemExit(main())
