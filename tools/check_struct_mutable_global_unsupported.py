#!/usr/bin/env python3
"""Separate szc-only mutable-global NYI and value-cycle guards; no official parity.
Prepared only; not researcher-executed. Never run generated programs natively.
"""
import argparse,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
BASE='const S=struct{a:u8,}; '
CASES=(
 ('tls',BASE+'threadlocal var g:S=.{.a=1};pub fn main()void{g.a=2;}',False),
 ('export-global',BASE+'export var g:S=.{.a=1};pub fn main()void{g.a=2;}',False),
 ('extern-global',BASE+'extern var g:S;pub fn main()void{g.a=2;}',False),
 ('packed','const S=packed struct{a:u8,};var g:S=.{.a=1};pub fn main()void{_= &g;}',False),
 ('default','const S=struct{a:u8=7,};var g:S=.{};pub fn main()void{_= &g;}',False),
 ('field-align','const S=struct{a:u8 align(16),};var g:S=.{.a=1};pub fn main()void{_= &g;}',False),
 ('whole-volatile-load',BASE+'var g:S=.{.a=1};pub fn main()void{const p:*volatile S=&g;const old=p.*;_=old;}',False),
 ('whole-volatile-store',BASE+'var g:S=.{.a=1};pub fn main()void{const p:*volatile S=&g;p.*=S{.a=2};}',False),
 ('initializer-function-call',BASE+'fn make()S{return .{.a=7};}var g:S=make();pub fn main()void{_=g.a;}',False),
 ('opv-global-comptime-read','const Z=struct{a:u0,v:void,};var a:Z=.{.a=0,.v={}};var b:Z=a;pub fn main()void{_= &b;}',False),
 ('huge-array-before-publication','const S=struct{a:u16,};var a:[9223372036854775808]S=undefined;pub fn main()void{_= &a;}',False),
 ('self-value-cycle',BASE+'var g:S=g;pub fn main()void{_=g.a;}',True),
 ('mutual-value-cycle',BASE+'var a:S=b;var b:S=a;pub fn main()void{_=a.a;}',True),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');args=p.parse_args();bad=[]
 with tempfile.TemporaryDirectory(prefix='szc-struct-mutable-global-nyi-') as directory:
  path=Path(directory)/'test.zig'
  for index,(label,source,cycle) in enumerate(CASES,1):
   path.write_text(source)
   r=run_compiler([args.szc,'emit-qbe',str(path)],source,f'struct mutable global NYI case {index} szc',timeout=120)
   text=r.stderr.decode(errors='replace')
   if r.returncode!=1 or not ('depend' in text.lower() if cycle else 'not supported by szc yet:' in text):bad.append(label+': '+str(primary_error(r.stderr)))
 print(f'struct mutable global scope: {len(CASES)-2} NYI gates; 2 dependency-cycle rejections; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
