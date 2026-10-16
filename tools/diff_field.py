#!/usr/bin/env python3
"""Compare @field reads, storage projections, names and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    prefix = ('const N=struct { const x:u8=7; const Byte=u8; const Self=@This(); '
              'var counter:u8=1; var data:[3]u8=.{1,2,3}; const aligned:u8 align(32)=9; '
              'const lazy_bad:u8=256; fn bad() u8 { return 256; } '
              'const @"space name":u8=11; const @"\\u{e9}":u8=13; const @"\\xff":u8=17; '
              'const @"u8":u8=19; const len:u8=23; const ptr:u8=29; '
              'fn add(n:u8) u8 { return n+1; } const Child=struct { const own:u8=31; }; }; ')
    for base in ('N','&N','@field(N,"Self")'):
        for name, expected in (('x',7),('space name',11),('\\xc3\\xa9',13),('\\u{e9}',13),
                                ('\\xff',17),('u8',19),('len',23),('ptr',29)):
            yield prefix + f'pub fn main() void {{ const n:u8=if(@field({base},"{name}")=={expected})0 else 256; _=n; }}'
    for name in ('"x"','".x."[1..2]','""++"x"','&[_]u8{120}','&.{120}'):
        yield prefix + f'pub fn main() void {{ const n:u8=if(@field(N,{name})==7)0 else 256; _=n; }}'
    for body in (
        'const n:@field(N,"Byte")=7; _=n;',
        'const n:u8=if(@field(N,"Self")==N)0 else 256; _=n;',
        'const n:u8=if(@field(@field(N,"Child"),"own")==31)0 else 256; _=n;',
        'const n:u8=if(@field(&N,"x")==N.x)0 else 256; _=n;',
        'const n:u8=if(&@field(N,"x")==&N.x)0 else 256; _=n;',
        'const n:u8=if(@intFromPtr(&@field(N,"aligned"))&31==0)0 else 256; _=n;',
        'const callback:*const fn(u8) u8=&@field(N,"add"); _=callback(3);',
        '_=@field(N,"add")(3);',
        '@field(N,"counter")=7; @field(N,"counter")+=2;',
        'const p=&@field(N,"counter"); p.*+=2;',
        '@field(N,"data")[1]=7; @field(N,"data")[2]+=1;',
        'const p=&@field(N,"data")[1]; p.*+=2;',
        '_=@field(N,"data")[1];',
        'const name:[]const u8="x"; _=@field(N,name);',
        'const bytes=[_]u8{120}; _=@field(N,&bytes);',
        'const p=&N; const q=&p; _=@field(p,"x"); _=@field(q.*,"x");',
    ):
        yield prefix + 'pub fn main() void { '+body+' }'
    for base in ('N','N.Child','@This()'):
        for name in ('missing','','x\\x00suffix','\\x00x'):
            yield prefix + f'pub fn main() void {{ _=@field({base},"{name}"); }}'
    for base in ('u8','bool','type','void','[2]u8','[]u8','*u8','anyopaque'):
        yield f'pub fn main() void {{ _=@field({base},"missing"); }}'
    for base in ('3','true','null','@as(type,undefined)'):
        for name in ('"x"','3','undefined'):
            yield f'pub fn main() void {{ _=@field({base},{name}); }}'
    for name in ('"x"','"lazy_bad"','"bad"'):
        yield prefix + f'pub fn main() void {{ const p=&@field(N,{name}); _=p; }}'
    for body in (
        '@field(N,"x")=3;',
        '_=@field(N,3);',
        '_=@field(N,undefined);',
        '_=@field(N,[_]u8{120});',
        '_=@field(N,.{120});',
        'var name=[_]u8{120}; _=&name; _=@field(N,&name);',
        'const p:*const [0]u8=undefined; _=@field(N,p);',
        'const p:*allowzero const [0]u8=@ptrFromInt(0); _=@field(N,p);',
        'const p:*const type=undefined; _=@field(p,"x");',
    ):
        yield prefix + 'pub fn main() void { '+body+' }'
    for source in (
        'fn f(name:[]const u8) u8 { return @field(N,name); } pub fn main() void { _=f("x"); }',
        'var name:[]const u8="x"; pub fn main() void { _=@field(N,name); }',
    ):
        yield prefix + source
    for ty in ('[0]u8','[3]u8','*[3]u8','*const [3]u8','*volatile [3]u8'):
        for use in ('_=@field(a,"len");','_=&@field(a,"len");','@field(a,"len")=1;', '_=@field(a,"bad");'):
            yield f'pub fn main() void {{ const a:{ty}=undefined; {use} }}'
    for ty in ('[]u16','[]const u16','*[]u16','*const []u16','*volatile []u16','*align(1) []u16','*allowzero []u16'):
        for field in ('len','ptr'):
            for use in (f'_=@field(s,"{field}");',f'_=&@field(s,"{field}");',f'@field(s,"{field}")=undefined;'):
                yield f'fn f(s:{ty}) void {{ {use} }} pub fn main() void {{ f(undefined); }}'
    for ty in ('[]u8','[]const u8','[2]u8','*u8','[*]u8'):
        yield f'pub fn main() void {{ const s:{ty}=undefined; _=@field(s,"unknown"); }}'
    for use in ('_=@field(N,"bad");', '_=N.bad;', '_=&N.bad;',
                'const p:*const fn() u8=@field(N,"bad"); _=p;',
                'const p:*const fn() u8=N.bad; _=p;'):
        yield prefix + 'pub fn main() void { '+use+' }'
    for name in ('x\\x00suffix', 'missing\\nname', '\\xff'):
        yield prefix + f'pub fn main() void {{ _=@field(N,"{name}"); }}'
    # Zig's built-in-field eqlSlice accepts a literal prefix terminated by NUL;
    # namespace hash lookup compares the complete normalized name.
    for name in ('len\\x00', 'len\\x00suffix', 'len\\x00ptr'):
        yield f'pub fn main() void {{ const a:[3]u8=undefined; const n:u8=if(@field(a,"{name}")==3)0 else 256; _=n; }}'
        yield f'pub fn main() void {{ var s:[]const u8="abc"; @field(s,"{name}")=1; }}'
        yield prefix + f'pub fn main() void {{ _=@field(N,"{name}"); }}'
    for name in ('ptr\\x00', 'ptr\\x00suffix'):
        yield f'pub fn main() void {{ const s:[]const u8="abc"; const n:u8=if(@field(s,"{name}")[0]==97)0 else 256; _=n; }}'
        yield prefix + f'pub fn main() void {{ _=@field(N,"{name}"); }}'
    for name in ('lenX', 'ptrX', '\\x00len'):
        yield f'pub fn main() void {{ const s:[]const u8=undefined; _=@field(s,"{name}"); }}'
    for name in ('"x\\x00"', '".x\\x00."[1..3]', '&[_]u8{120,0}', '&[_:0]u8{120,0}'):
        yield prefix + f'pub fn main() void {{ const n:u8=if(@field(N,{name})==7)0 else 256; _=n; }}'
    for name in ('"x\\x00\\x00"', '"x\\x00suffix\\x00"', '&[_]u8{120,0,0}', '"\\x00"'):
        yield prefix + f'pub fn main() void {{ _=@field(N,{name}); }}'
    long_name = 'member_' + 'x' * 700
    yield f'const N=struct {{ const @"{long_name}":u8=7; }}; pub fn main() void {{ _=@field(N,"{long_name}"); }}'
    yield f'const N=struct {{}}; pub fn main() void {{ _=@field(N,"{long_name}"); }}'
    for args in ('','N','N,"x",3'):
        for use in (f'_=@field({args});',f'_=&@field({args});',f'@field({args})=3;'):
            yield prefix + 'pub fn main() void { '+use+' }'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-field-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_field case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_field case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'field: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
