#!/usr/bin/env python3
"""Separate szc-only M6.1g scope gates; intentionally no official parity promise.
Prepared only; not executed by reference researcher. Parent owns gate integration.
"""
import argparse,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
BASE='const S=struct{a:u8,}; '
CASES=(
 ('byvalue-parameter',BASE+'fn f(s:S)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}'),
 ('byvalue-return',BASE+'fn f()S{return .{.a=1};}pub fn main()void{_=f();}'),
 ('recursive-array-parameter',BASE+'fn f(a:[2][1]S)u8{return a[0][0].a;}pub fn main()void{_=f(.{.{.{.a=1}},.{.{.a=2}}});}'),
 ('recursive-array-return',BASE+'fn f()[1]S{return .{.{.a=1}};}pub fn main()void{_=f();}'),
 ('mutable-global',BASE+'var s:S=.{.a=1};pub fn main()void{s.a=2;}'),
 ('mutable-global-array',BASE+'var a:[1]S=.{.{.a=1}};pub fn main()void{a[0].a=2;}'),
 ('volatile-whole-load',BASE+'pub fn main()void{var s:S=.{.a=1};const p:*volatile S=&s;const copy=p.*;_=copy;}'),
 ('volatile-whole-store',BASE+'pub fn main()void{var s:S=.{.a=1};const p:*volatile S=&s;p.*=S{.a=2};}'),
 ('cabi-struct-parameter',BASE+'export fn consume(s:S)u8{return s.a;}pub fn main()void{}'),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');args=p.parse_args();bad=[]
 with tempfile.TemporaryDirectory(prefix='szc-struct-runtime-nyi-') as directory:
  path=Path(directory)/'test.zig'
  for i,(label,src) in enumerate(CASES,1):
   path.write_text(src)
   r=run_compiler([args.szc,'emit-qbe',str(path)],src,f'struct runtime NYI case {i} szc',timeout=120)
   if r.returncode!=1 or 'not supported by szc yet:' not in r.stderr.decode(errors='replace'):bad.append(label+': '+str(primary_error(r.stderr)))
 print(f'struct runtime NYI: {len(CASES)} cases; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
