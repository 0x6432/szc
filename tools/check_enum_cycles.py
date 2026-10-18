#!/usr/bin/env python3
"""Separate demanded enum tag cycles: failure status, not exact diagnostic parity.
A cycle is never success even if a compiler emits an unlabeled dependency report.
"""
import argparse,json,tempfile
from pathlib import Path
from compiler_process import run_compiler
CASES=(
 ('self-size','const E=enum(u8){a=@sizeOf(@This()),b};pub fn main()void{_=E.a;}\n'),
 ('prior-tag','const E=enum(u8){a=1,b=@intFromEnum(E.a)+1};pub fn main()void{_=E.b;}\n'),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');p.add_argument('zig');p.add_argument('--reference-only',action='store_true');p.add_argument('--report',type=Path);a=p.parse_args();rows=[];bad=[]
 with tempfile.TemporaryDirectory(prefix='szc-enum-cycles-')as directory:
  path=Path(directory)/'test.zig'
  for i,(label,source)in enumerate(CASES,1):
   path.write_text(source)
   for who,compiler in ([('zig',a.zig)]if a.reference_only else [('szc',a.szc),('zig',a.zig)]):
    cmd=[compiler,'build-exe',str(path),'-fno-emit-bin','-j2']if who=='zig'else[compiler,'emit-qbe',str(path)]
    b=run_compiler(cmd,source,f'enum cycles case {i} {who}',timeout=120);text=b.stderr.decode(errors='replace')
    ok=b.returncode==1 and ('depends on itself' in text or 'dependency loop' in text or 'cyclic' in text or 'cycle' in text or 'recursive' in text)
    rows.append(dict(index=i,label=label,compiler=who,source=source,status=b.returncode,stderr=text,ok=ok))
    if not ok:bad.append(who+' '+label+' expected demanded cyclic dependency failure\n'+text)
    if a.report:a.report.write_text(json.dumps(rows,indent=2)+'\n')
 print(f'enum cycles: {len(rows)} checks; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
