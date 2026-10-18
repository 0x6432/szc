#!/usr/bin/env python3
"""szc-only rejection gates for explicitly out-of-scope struct features.

Not a Zig differential: these constructs include valid Zig, and szc must
report its explicit NYI rather than pretend they are invalid Zig types.
"""
import argparse
from pathlib import Path
import re
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE='emit-qbe'
NYI=re.compile(r'not supported|unsupported|not (?:yet )?implemented|NYI',re.I)


# Ordinary struct @bitSizeOf is an exact invalid-Zig diagnostic, not NYI.
# Its precedence/array cases live in diff_struct_bit_layout and unit/golden tests.
def cases():
    for label, declaration in (
        ('packed', 'packed struct { x:u8, }'),
        ('extern', 'extern struct { x:u8, }'),
        ('tuple', 'struct { u8, u16 }'),
        ('comptime-field', 'struct { comptime x:u8=1, }'),
        ('valid-default', 'struct { x:u8=7, }'),
        ('invalid-default', 'struct { x:u8=256, }'),
        ('explicit-align', 'struct { x:u8 align(16), }'),
    ):
        yield label, 'const S='+declaration+'; pub fn main() void { _=@sizeOf(S); }'
    for ty in ('noreturn','comptime_int','type','fn() void'):
        for query in ('@hasField(S,"x")','@FieldType(S,"x")','@sizeOf(S)','@alignOf(S)'):
            yield ty+'/'+query, 'const S=struct { x:'+ty+', }; pub fn main() void { _='+query+'; }'
    yield 'runtime-struct-value', 'const S=struct { x:u8, }; fn f(x:u8) u8 { const s:S=.{.x=x}; return s.x; } pub fn main() u8 { return f(7)-7; }'

    prefix = 'const S=struct { x:u8, }; '
    for label, body in (
        ('local-storage', 'pub fn main() void { var s:S=undefined; _=&s; }'),
        ('global-storage', 'var s:S=undefined; pub fn main() void { _=&s; }'),
        ('store-undefined', 'fn f(p:*S) void { p.*=undefined; } pub fn main() void { f(@ptrFromInt(4096)); }'),
        ('volatile-store-undefined', 'fn f(p:*volatile S) void { p.*=undefined; } pub fn main() void { f(@ptrFromInt(4096)); }'),
        ('direct-argument', 'fn f(s:S) void { _=s; } pub fn main() void { f(undefined); }'),
        ('return', 'fn f() S { return undefined; } pub fn main() void { _=f(); }'),
        ('indirect-argument', 'fn f(p:*const fn(S) void) void { p(undefined); } pub fn main() void { f(@ptrFromInt(4096)); }'),
        ('variadic-argument', 'extern fn f(...) void; pub fn main() void { f(@as(S,undefined)); }'),
        ('runtime-join', 'fn f(c:bool) void { const s=if(c)@as(S,undefined) else @as(S,undefined); _=s; } pub fn main() void { f(true); }'),
        ('array-storage', 'pub fn main() void { var a:[1]S=undefined; _=&a; }'),
    ):
        yield label, prefix + body
    yield 'runtime-struct-load', prefix+'fn f(p:*S) void { const value=p.*; _=value; } pub fn main() void { const p:*S=@ptrFromInt(4096); f(p); }'
    for query in ('@hasField(S,"x")', '@FieldType(S,"x")', '@sizeOf(S)', '@alignOf(S)'):
        for ty in ('f16','f80','f128'):
            yield 'wide-float/'+ty+'/'+query, 'const S=struct { x:'+ty+', }; pub fn main() void { _='+query+'; }'
    yield 'field-size-overflow', 'const S=struct { x:[18446744073709551615]u16, }; pub fn main() void { _=@sizeOf(S); }'
    yield 'struct-size-overflow', 'const S=struct { x:[18446744073709551615]u8,y:u8, }; pub fn main() void { _=@sizeOf(S); }'
    yield 'struct-padding-overflow', 'const S=struct { x:[18446744073709551608]u8,y:u64, }; pub fn main() void { _=@sizeOf(S); }'


# Preserve former NYIs as supported emission controls after local runtime storage.
CONTROLS = {
    'runtime-struct-value', 'local-storage', 'store-undefined',
    'runtime-join', 'array-storage', 'runtime-struct-load',
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-struct-nyi-') as directory:
        path=Path(directory)/'test.zig'
        for index,(label,source) in enumerate(cases(),1):
            path.write_text(source+'\n')
            result=run_compiler([args.szc,SZC_MODE,str(path)],source,f'struct NYI {index} {label}',timeout=120)
            error=primary_error(result.stderr)
            if label in CONTROLS:
                if result.returncode != 0:
                    raise SystemExit(f'expected supported control: {label}\n{source}\n'+result.stderr.decode(errors='replace'))
            elif result.returncode!=1 or not error or not NYI.search(error):
                raise SystemExit(f'expected explicit NYI: {label}\n{source}\n'+result.stderr.decode(errors='replace'))
    print(f'struct metadata: {index-len(CONTROLS)} explicit unsupported-feature rejections; {len(CONTROLS)} supported controls')

if __name__=='__main__':
    main()
