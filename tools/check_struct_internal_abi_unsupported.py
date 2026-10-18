#!/usr/bin/env python3
"""Separate szc-only internal-ABI scope guards; not official parity, not researcher-run."""
import argparse,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
BASE='const S=struct{a:u8,}; '
# Each row names an independent closed gate. Conventional C errors are legitimate,
# not necessarily an NYI prefix; accept either without requiring a new diagnostic.
CASES=(
 ('direct-C-parameter',BASE+'fn f(s:S)callconv(.c)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}',('calling convention','not supported by szc yet:')),
 ('direct-C-return',BASE+'fn f()callconv(.c)S{return .{.a=1};}pub fn main()void{_=f();}',('calling convention','not supported by szc yet:')),
 ('indirect-C-parameter',BASE+'pub fn main()void{const p:*const fn(S)callconv(.c)u8=@ptrFromInt(4096);_=p(.{.a=1});}',('calling convention','not supported by szc yet:')),
 ('indirect-C-return',BASE+'pub fn main()void{const p:*const fn()callconv(.c)S=@ptrFromInt(4096);_=p();}',('calling convention','not supported by szc yet:')),
 ('direct-SysV',BASE+'fn f(s:S)callconv(.x86_64_sysv)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}',('calling convention','not supported by szc yet:')),
 ('indirect-SysV',BASE+'pub fn main()void{const p:*const fn(S)callconv(.x86_64_sysv)u8=@ptrFromInt(4096);_=p(.{.a=1});}',('calling convention','not supported by szc yet:')),
 ('extern-recursive-array',BASE+'extern fn f(a:[2][1]S)void;pub fn main()void{f(.{.{.{.a=1}},.{.{.a=2}}});}',('calling convention','not supported by szc yet:')),
 ('export-struct-return',BASE+'export fn f()S{return .{.a=1};}pub fn main()void{}',('calling convention','not supported by szc yet:')),
 ('mutable-global',BASE+'var s:S=.{.a=1};pub fn main()void{s.a=2;}',('not supported by szc yet:',)),
 ('volatile-whole-return-source',BASE+'fn f(p:*const volatile S)S{return p.*;}pub fn main()void{var s:S=.{.a=1};_=f(&s);}',('not supported by szc yet:',)),
 ('default-field-signature','const S=struct{a:u8=7,};fn f(s:S)S{return s;}pub fn main()void{_=f(.{.a=1});}',('not supported by szc yet:',)),
 ('generic-call',BASE+'fn f(comptime T:type,s:T)T{return s;}pub fn main()void{_=f(S,.{.a=1});}',('not supported by szc yet:',)),
)
# M6.1i: retain the former mutable-global rejection as an emission control.
CONTROLS = {'mutable-global'}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');args=p.parse_args();bad=[]
 with tempfile.TemporaryDirectory(prefix='szc-struct-internal-abi-nyi-') as directory:
  path=Path(directory)/'test.zig'
  for i,(label,source,allowed) in enumerate(CASES,1):
   path.write_text(source)
   r=run_compiler([args.szc,'emit-qbe',str(path)],source,f'struct internal ABI NYI case {i} szc',timeout=120)
   text=r.stderr.decode(errors='replace')
   if label in CONTROLS:
    if r.returncode!=0:bad.append('expected supported control '+label+': '+str(primary_error(r.stderr)))
   elif r.returncode!=1 or not any(s in text for s in allowed):bad.append(label+': '+str(primary_error(r.stderr)))
 print(f'struct internal ABI NYI: {len(CASES)-len(CONTROLS)} NYI cases; {len(CONTROLS)} supported controls; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
