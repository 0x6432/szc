#!/usr/bin/env python3
"""Separate szc-only static-struct boundary gates; no official parity promise."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

BASE='const S=struct {a:u8,}; '
ESCAPE='extern fn consume(p:*const anyopaque) void; '
CASES=(
 ('mutable-global',BASE+ESCAPE+'var s:S=.{.a=7}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('mutable-local',BASE+ESCAPE+'pub fn main() void {var s:S=.{.a=7}; consume(@ptrCast(&s));}'),
 ('runtime-init',BASE+'fn byte() u8 {return 7;} pub fn main() void {const s=S{.a=byte()}; _=s;}'),
 ('runtime-parameter',BASE+'fn f(s:S) u8 {return s.a;} pub fn main() void {_=f(.{.a=7});}'),
 ('runtime-return',BASE+'fn f() S {return .{.a=7};} pub fn main() void {_=f();}'),
 ('runtime-array-index',BASE+'const a=[_]S{.{.a=7},.{.a=8}}; fn f(i:usize) u8 {return a[i].a;} pub fn main() void {_=f(1);}'),
 ('mutable-array',BASE+ESCAPE+'pub fn main() void {var a=[_]S{.{.a=7}}; consume(@ptrCast(&a));}'),
 ('dynamic-root-field-address',BASE+'const s:S=.{.a=7}; fn f(p:*const S) *const u8 {return &p.a;} pub fn main() void {_=f(&s);}'),
 ('absolute-root-field-address',BASE+'pub fn main() void {const p:*const S=@ptrFromInt(4096); _=&p.a;}'),
 ('volatile-whole-read',BASE+'const s:S=.{.a=7}; pub fn main() void {const p:*const volatile S=&s; _=p.*;}'),
 ('known-reinterpret-load',BASE+'const s:S=.{.a=7}; pub fn main() void {const p:*const u8=@ptrCast(&s); _=p.*;}'),
 ('wide-physical-storage',ESCAPE+'const S=struct {a:u128,}; const s:S=.{.a=1}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('float-physical-storage',ESCAPE+'const S=struct {a:f32,}; const s:S=.{.a=undefined}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('default-layout',ESCAPE+'const S=struct {a:u8=7,}; const s:S=.{}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('aligned-layout',ESCAPE+'const S=struct {a:u8 align(16),}; const s:S=.{.a=7}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('packed-layout',ESCAPE+'const S=packed struct {a:u8,}; const s:S=.{.a=7}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('extern-layout',ESCAPE+'const S=extern struct {a:u8,}; const s:S=.{.a=7}; pub fn main() void {consume(@ptrCast(&s));}'),
 ('typed-self-address-cycle','const S=struct {p:*const S,}; const a:S=.{.p=&a}; pub fn main() void {_=@intFromPtr(a.p);}'),
 ('typed-mutual-address-cycle','const S=struct {p:*const S,}; const a:S=.{.p=&b}; const b:S=.{.p=&a}; pub fn main() void {_=@intFromPtr(a.p);}'),
)

CONTROLS = {
 'mutable-local', 'runtime-init', 'runtime-array-index', 'mutable-array',
 'dynamic-root-field-address', 'absolute-root-field-address',
 # M6.1h fixed internal value parameters/results.
 'runtime-parameter', 'runtime-return',
 # M6.1i mutable symbols; immutable address cycles remain closed.
 'mutable-global',
}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('szc');args=parser.parse_args()
    failures=[]
    with tempfile.TemporaryDirectory(prefix='szc-struct-static-nyi-') as directory:
        path=Path(directory)/'test.zig'
        for index,(label,source) in enumerate(CASES,1):
            path.write_text(source)
            result=run_compiler([args.szc,'emit-qbe',str(path)],source,f'struct static NYI case {index} szc',timeout=120)
            error=primary_error(result.stderr)
            # Cycles may retain existing dependency diagnostics rather than an NYI.
            cycle='address-cycle' in label
            diagnostic=result.stderr.decode(errors='replace')
            if label in CONTROLS:
                if result.returncode != 0:
                    failures.append(label+': '+str(error))
            elif result.returncode!=1 or not ('not supported by szc yet:' in diagnostic or (cycle and 'depend' in diagnostic.lower())):
                failures.append(label+': '+str(error))
    print(f'struct static NYI: {len(CASES)-len(CONTROLS)} NYI cases, {len(CONTROLS)} supported controls; {len(failures)} failures')
    if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
