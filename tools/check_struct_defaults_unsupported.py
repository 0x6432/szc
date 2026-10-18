#!/usr/bin/env python3
"""Separate defaults dependency failures and established szc-only scope gates.
--reference-only probes only dependency failures with official Zig. NYIs are
intentionally not reference parity; None primary never means success.
"""
import argparse,json,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
CYCLES=(
 ('self-default','const S=struct{x:u8=(S{}).x,};pub fn main()void{const s:S=.{};_=s;}'),
 ('mutual-default','const A=struct{x:u8=(B{}).x,};const B=struct{x:u8=(A{}).x,};pub fn main()void{const a:A=.{};_=a;}'),
 ('byvalue-layout','const S=struct{x:S=.{},};pub fn main()void{_=@sizeOf(S);}'),
)
CASES=(
 ('default-call','fn make()u8{return 7;}const S=struct{x:u8=make(),};pub fn main()void{const s:S=.{};_=s;}'),
 ('default-comptime-mutation','const S=struct{x:u8=blk:{var n:u8=3;n+=4;break :blk n;},};pub fn main()void{const s:S=.{};_=s;}'),
 ('comptime-field','const S=struct{comptime x:u8=7,};pub fn main()void{const s:S=.{};_=s;}'),
 ('aligned-field','const S=struct{x:u8 align(16)=7,};pub fn main()void{const s:S=.{};_=s;}'),
 ('packed-field','const S=packed struct{x:u8=7,};pub fn main()void{const s:S=.{};_=s;}'),
 ('extern-field','const S=extern struct{x:u8=7,};pub fn main()void{const s:S=.{};_=s;}'),
 ('opv-global-comptime-read','const Z=struct{a:u0=0,v:void={},};var a:Z=.{};var b:Z=a;pub fn main()void{_= &b;}'),
 ('whole-volatile-default','const S=struct{x:u8=7,};var g:S=.{};pub fn main()void{const p:*volatile S=&g;const old=p.*;_=old;}'),
 ('C-ABI-default','const S=struct{x:u8=7,};fn f(s:S)callconv(.c)u8{return s.x;}pub fn main()void{_=f(.{});}'),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('compiler');p.add_argument('--reference-only',action='store_true');p.add_argument('--report',type=Path);a=p.parse_args();records=[];fail=[]
 with tempfile.TemporaryDirectory(prefix='szc-struct-defaults-scope-') as directory:
  path=Path(directory)/'test.zig'
  for i,(label,source) in enumerate(CYCLES+(() if a.reference_only else CASES),1):
   path.write_text(source);cycle=i<=len(CYCLES)
   cmd=[a.compiler,'build-exe',str(path),'-fno-emit-bin','-j2'] if a.reference_only else [a.compiler,'emit-qbe',str(path)]
   b=run_compiler(cmd,source,f'struct defaults scope case {i} '+('zig' if a.reference_only else 'szc'),timeout=120)
   text=b.stderr.decode(errors='replace');error=primary_error(b.stderr)
   records.append(dict(label=label,source=source,status=b.returncode,primary=error,stderr=text))
   okay=b.returncode==1 and (('depend' in text.lower() or 'cycle' in text.lower()) if cycle else ('not supported by szc yet:' in text or (label=='C-ABI-default' and 'calling convention' in text)))
   if not okay:fail.append(label+': '+repr(error)+'\n'+text)
 if a.report:a.report.write_text(json.dumps(records,indent=2)+'\n')
 print(f'struct defaults scope: {len(records)} cases; {len(fail)} failures')
 if fail:raise SystemExit('\n'.join(fail))
if __name__=='__main__':main()
