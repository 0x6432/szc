#!/usr/bin/env python3
"""Separate szc-only deferred method forms / closed ABI and storage gates.
Not official diagnostic parity. Never execute generated programs natively.
"""
import argparse,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
CASES=(
 ('explicit-callconv-pointer', 'const S=struct{x:u8,fn read(s:*const S)callconv(.c)u8{return s.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}\n', ('not supported by szc yet:',)),
 ('implicit-C-export-aggregate', 'const S=struct{x:u8,export fn read(s:S)u8{return s.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}', ('calling convention', 'not supported by szc yet:')),
 ('generic-self','const S=struct{x:u8,fn read(s:anytype)u8{return s.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}',('not supported by szc yet:',)),
 ('comptime-argument','const S=struct{x:u8,fn read(s:S,comptime n:u8)u8{return s.x+n;}};pub fn main()void{const s:S=.{.x=7};_=s.read(1);}',('not supported by szc yet:',)),
 ('comptime-method-call','const S=struct{x:u8,fn read(s:S)u8{return s.x;}};const n=(S{.x=7}).read();pub fn main()u8{return n-7;}',('not supported by szc yet:',)),
 ('optional-self','const S=struct{x:u8,fn read(s:?S)u8{return s.?.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}',('not supported by szc yet:',)),
 ('error-union-self','const S=struct{x:u8,fn read(s:anyerror!S)u8{const v=s catch return 0;return v.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}',('not supported by szc yet:',)),
 ('whole-volatile-value','const S=struct{x:u8,fn read(s:S)u8{return s.x;}};var g:S=.{.x=7};pub fn main()void{const p:*volatile S=&g;_=p.read();}',('not supported by szc yet:',)),
 ('C-value-self','const S=struct{x:u8,fn read(s:S)callconv(.c)u8{return s.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}',('calling convention','not supported by szc yet:')),
 ('C-aggregate-return','const S=struct{x:u8,fn clone(s:*const S)callconv(.c)S{return s.*;}};pub fn main()void{const s:S=.{.x=7};_=s.clone();}',('calling convention','not supported by szc yet:')),
 ('packed-self','const S=packed struct{x:u8,fn read(s:S)u8{return s.x;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}',('not supported by szc yet:',)),
 ('OPV-global-read','const S=struct{x:u0=0,fn read(s:S)u0{return s.x;}};var a:S=.{};var b:S=a;pub fn main()void{_=b.read();}',('not supported by szc yet:',)),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');a=p.parse_args();bad=[]
 with tempfile.TemporaryDirectory(prefix='szc-struct-methods-scope-')as directory:
  path=Path(directory)/'test.zig'
  for i,(label,source,allowed)in enumerate(CASES,1):
   path.write_text(source);b=run_compiler([a.szc,'emit-qbe',str(path)],source,f'struct methods scope case {i} szc',timeout=120);text=b.stderr.decode(errors='replace')
   if b.returncode!=1 or not any(x in text for x in allowed):bad.append(label+': '+str(primary_error(b.stderr))+'\n'+text)
 print(f'struct methods scope: {len(CASES)} closed gates; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
