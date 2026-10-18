#!/usr/bin/env python3
"""Require explicit szc NYI for deferred struct storage/address/ABI features.

This is NOT reference-parity testing: the deferred constructs include valid Zig.
"""
import argparse
from pathlib import Path
import re
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE='emit-qbe'
NYI=re.compile(r'not supported|unsupported|not (?:yet )?implemented|NYI',re.I)


def all_cases():
    prefix='const S=struct { a:u8,b:u64, }; '
    for label,source in (
        ('mutable-local','pub fn main() void { var s=S{.a=1,.b=2}; s.a=3; _=s; }'),
        ('mutable-context','pub fn main() void { var s:S=.{.a=1,.b=2}; s.a=3; _=s; }'),
        ('global-struct-storage','var s=S{.a=1,.b=2}; pub fn main() u8 { return s.a; }'),
        ('runtime-field-init','fn f(a:u8) u8 { const s=S{.a=a,.b=2}; return s.a; } pub fn main() u8 { return f(0); }'),
        ('runtime-context-field-init','fn f(a:u8) u8 { const s:S=.{.a=a,.b=2}; return s.a; } pub fn main() u8 { return f(0); }'),
        ('struct-parameter','fn f(s:S) u8 { return s.a; } pub fn main() u8 { return f(S{.a=0,.b=2}); }'),
        ('struct-return','fn f() S { return S{.a=0,.b=2}; } pub fn main() u8 { return f().a; }'),
        ('escaped-struct-address','extern fn consume(address:usize) void; pub fn main() void { const s=S{.a=1,.b=2}; consume(@intFromPtr(&s)); }'),
        ('field-address','pub fn main() void { const s=S{.a=1,.b=2}; const p=&s.a; _=p; }'),
        ('builtin-field-address','pub fn main() void { const s=S{.a=1,.b=2}; const p=&@field(s,"a"); _=p; }'),
        ('pointer-instance-projection','fn f(p:*const S) u8 { return p.a; } pub fn main() void { const p:*const S=@ptrFromInt(4096); _=f(p); }'),
        ('whole-struct-load','fn f(p:*const S) void { const s=p.*; _=s; } pub fn main() void { const p:*const S=@ptrFromInt(4096); f(p); }'),
        ('runtime-struct-choice','fn f(b:bool) u8 { const s=if(b) S{.a=1,.b=2} else S{.a=3,.b=4}; return s.a; } pub fn main() void { _=f(true); }'),
        ('dynamic-struct-array-index','fn f(i:usize) u8 { const a=[_]S{S{.a=1,.b=2},S{.a=3,.b=4}}; return a[i].a; } pub fn main() void { _=f(0); }'),
        ('escaped-struct-array-slice-backing','extern fn consume(p:[*]const S,len:usize) void; pub fn main() void { const a=[_]S{S{.a=1,.b=2}}; const s:[]const S=&a; consume(s.ptr,s.len); }'),
    ):
        yield label,prefix+source
    for label,definition,init in (
        ('tuple','struct { u8,u16 }','@as(S,.{1,2})'),
        ('default','struct { a:u8=1, }','S{}'),
        ('comptime','struct { comptime a:u8=1, }','S{}'),
        ('explicit-align','struct { a:u8 align(16), }','S{.a=1}'),
        ('packed','packed struct { a:u8, }','S{.a=1}'),
        ('extern','extern struct { a:u8, }','S{.a=1}'),
    ):
        yield label,'const S='+definition+'; pub fn main() void { const s='+init+'; _=s; }'

    # Independently reviewed provenance and volatile-load boundaries.
    yield 'backing/escape_array_element', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [2]S{ obj, obj };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&items[1]); }\n'
    yield 'backing/escape_direct', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n'
    yield 'backing/escape_empty', 'const S = struct {};\nconst obj = S{};\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n'
    yield 'backing/escape_erased', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const anyopaque) void { _ = p; }\npub fn main() void { use(@ptrCast(&obj)); }\n'
    yield 'backing/escape_global_alias_cold', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const S = &obj;\nfn use(p: *const *const S) void { _ = p; }\npub fn main() void { use(&alias); }\n'
    yield 'backing/escape_global_alias_resolved', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const S = &obj;\nfn use(p: *const *const S) void { _ = p; }\npub fn main() void { _ = alias; use(&alias); }\n'
    yield 'backing/escape_global_erased_alias', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const anyopaque = @ptrCast(&obj);\nfn use(p: *const *const anyopaque) void { _ = p; }\npub fn main() void { use(&alias); }\n'
    yield 'backing/escape_global_pointer', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nvar alias: *const S = &obj;\npub fn main() void { _ = alias; }\n'
    yield 'backing/escape_hidden_erased_pointer_array', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]*const anyopaque{ @ptrCast(&obj) };\nfn use(p: *const [1]*const anyopaque) void { _ = p; }\npub fn main() void { use(&items); }\n'
    yield 'backing/escape_hidden_pointer_array', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [2]*const S{ &obj, &obj };\nfn use(p: *const [2]*const S) void { _ = p; }\npub fn main() void { use(&items); }\n'
    yield 'backing/escape_integer', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: usize) void { _ = p; }\npub fn main() void { use(@intFromPtr(&obj)); }\n'
    yield 'backing/escape_slice', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\nfn use(p: []const S) void { _ = p; }\npub fn main() void { use(&items); }\n'
    yield 'backing/escape_undefined', 'const S = struct { a: u8 };\nconst obj: S = undefined;\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n'
    yield 'backing/escape_zero_array', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items: [0]S = .{};\nfn use(p: *const [0]S) void { _ = p; }\npub fn main() void { use(&items); }\n'
    yield 'backing/volatile_array', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\npub fn main() u8 { const p: *const volatile [1]S = &items; return p[0].a; }\n'
    yield 'backing/volatile_deref', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() void { const p: *const volatile S = &obj; _ = p.*; }\n'
    yield 'backing/volatile_dot', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p: *const volatile S = &obj; return p.a; }\n'
    yield 'backing/volatile_many', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\npub fn main() u8 { const p: [*]const volatile S = &items; return p[0].a; }\n'
    yield 'backing/volatile_named', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p: *const volatile S = &obj; return @field(p, "a"); }\n'



# M6.1f backing, M6.1g locals and M6.1h internal ABI retain former NYI sources. Keep
# every former rejection as an emission acceptance control instead of dropping it.
STATIC_CONTROLS = {
    'escaped-struct-address', 'field-address', 'builtin-field-address',
    'escaped-struct-array-slice-backing',
    'mutable-local', 'mutable-context', 'runtime-field-init',
    'runtime-context-field-init', 'pointer-instance-projection',
    'whole-struct-load', 'runtime-struct-choice', 'dynamic-struct-array-index',
    'backing/volatile_array', 'backing/volatile_dot', 'backing/volatile_many',
    'backing/volatile_named',
    # M6.1h internal ordinary-struct value signatures.
    'struct-parameter', 'struct-return',
    # M6.1i mutable internal struct storage.
    'global-struct-storage', 'default',
}


def is_static_control(label):
    return label in STATIC_CONTROLS or label.startswith('backing/escape_')


def cases():
    return ((label, source) for label, source in all_cases()
            if not is_static_control(label))


def controls():
    yield from ((label, source) for label, source in all_cases()
                if is_static_control(label))
    yield 'control_absolute', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(@ptrFromInt(4096)); }\n'
    yield 'control_discard_erased', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() void { const p: *const anyopaque = @ptrCast(&obj); _ = p; }\n'
    yield 'control_hidden_absolute', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]*const S{ @ptrFromInt(4096) };\nfn use(p: *const [1]*const S) void { _ = p; }\npub fn main() void { use(&items); }\n'
    yield 'control_logical_only', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p = &obj; return p.a; }\n'
    yield 'control_runtime', 'const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn relay(p: *const S) void { _ = p; }\nfn use(p: *const S) void { relay(p); }\npub fn main() void { use(@ptrFromInt(4096)); }\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-struct-values-nyi-') as directory:
        path=Path(directory)/'test.zig'
        for index,(label,source) in enumerate(cases(),1):
            path.write_text(source)
            a=run_compiler([args.szc,SZC_MODE,str(path)],source,f'check_struct_values_unsupported case {index} szc',timeout=120)
            error=primary_error(a.stderr)
            if a.returncode!=1 or error is None or not NYI.search(error):
                raise SystemExit(f'expected explicit NYI: {label}\n{source}\n'+a.stderr.decode(errors='replace'))
        negative_count=index
        for index,(label,source) in enumerate(controls(),1):
            path.write_text(source)
            a=run_compiler([args.szc,SZC_MODE,str(path)],source,f'check_struct_values_unsupported control {index} szc',timeout=120)
            if a.returncode!=0:
                raise SystemExit(f'expected supported control: {label}\n{source}\n'+a.stderr.decode(errors='replace'))
    print(f'struct values: {negative_count} explicit deferred-feature rejections; {index} supported controls')

if __name__=='__main__':
    main()
