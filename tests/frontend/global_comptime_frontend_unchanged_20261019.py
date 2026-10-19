#!/usr/bin/env python3
"""Byte-exact IR controls against the frozen diagnostic candidate, no filtering."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from compiler_process import run

CASES = [
 ('runtime_statement', 'pub fn main()void{@as(u8,3);}'),
 ('runtime_defer', 'pub fn main()void{defer {@as(u8,3);}}'),
 ('named_constant', 'const x=blk:{@as(u8,3);break :blk 4;};pub fn main()void{_=x;}'),
 ('fresh_function_in_namespace', 'comptime{const N=struct{fn f()void{@as(u8,3);}};_=N;} pub fn main()void{}'),
 ('fresh_test_in_namespace', 'comptime{const N=struct{test{@as(u8,3);}};_=N;} pub fn main()void{}'),
 ('fresh_named_member', 'comptime{const N=struct{const x=blk:{@as(u8,3);break :blk 4;};};_=N;} pub fn main()void{}'),
 ('consumed_namespace_value', 'comptime{const x:u8=blk:{break :blk if(true) @as(u8,3) else @as(u8,4);};_=x;} pub fn main()void{}'),
 ('discard_namespace_value', 'comptime{_=switch(@as(u8,0)){0=>@as(u8,3),else=>@as(u8,4)};} pub fn main()void{}'),
]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', required=True)
    p.add_argument('--candidate', required=True)
    p.add_argument('--report', required=True)
    a=p.parse_args()
    compilers=[('baseline',str(Path(a.baseline).resolve())),('candidate',str(Path(a.candidate).resolve()))]
    rows=[]
    with tempfile.TemporaryDirectory(prefix='szc-comptime-unchanged-') as temporary:
        for name, source in CASES:
            file=Path(temporary)/(name+'.zig');file.write_text(source+'\n')
            row={'case':name,'source':source,'results':{}}
            outputs=[]
            for label,binary in compilers:
                argv=[binary,'ir',str(file)]
                r=run(argv,timeout=30)
                outputs.append((r.returncode,r.stdout,r.stderr))
                row['results'][label]={'argv':argv,'exit':r.returncode,'stdout':r.stdout.decode(errors='replace'),'stderr':r.stderr.decode(errors='replace')}
            row['pass']=outputs[0]==outputs[1] and outputs[0][0]==0
            print('PASS' if row['pass'] else 'FAIL',name,flush=True)
            rows.append(row)
    Path(a.report).write_text(json.dumps({'cases':rows},indent=2)+'\n')
    return int(any(not row['pass'] for row in rows))

if __name__=='__main__':
    raise SystemExit(main())
