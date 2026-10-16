#!/usr/bin/env python3
"""Compare lazy namespace @hasDecl and constant byte-slice names with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    prefix = ('const outer:u8=1; const N=struct { pub const Self=@This(); pub const x:u8=256; '
              'pub var counter:u8=1; pub const Byte=u8; pub const public:u8=7; '
              'pub const len:usize=3; pub const ptr:u8=2; pub const @"space name":u8=1; '
              'pub const @"\\u{e9}":u8=1; pub const @"\\xff":u8=1; pub const @"u8":u8=1; '
              'pub fn bad() u8 { return 256; } pub const Child=struct { pub const own:u8=3; }; }; ')
    for base, present, absent in (
        ('N', ('Self','x','counter','Byte','public','len','ptr','space name','\\u{e9}','\\xc3\\xa9','\\xff','u8','bad','Child'),
         ('missing','outer','own','','x\\x00suffix','\\x00x')),
        ('N.Child', ('own',), ('x','outer','Child','Self')),
        ('@This()', ('main',), ('N','outer','x','own','Self')),
    ):
        for expected, names in ((True,present),(False,absent)):
            for name in names:
                test = f'@hasDecl({base},"{name}")'
                if not expected:
                    test = '!' + test
                yield prefix + f'pub fn main() void {{ const n:u8=if({test})0 else 256; _=n; }}'
    for expression in ('"x"', '"x"[0..]', '".x."[1..2]', '"xTAIL"[0..1]',
                       '""++"x"', '"x"++""', '&[_]u8{120}', '&[_:0]u8{120}',
                       '@as([]const u8,"x")', '@as([*:0]const u8,"x")[0..1]',
                       '@as(*const [1]u8,"x")', '&.{120}'):
        yield prefix + f'pub fn main() void {{ const n:u8=if(@hasDecl(N,{expression}))0 else 256; _=n; }}'
    for body in (
        'const bytes=[_]u8{120}; const n:u8=if(@hasDecl(N,&bytes))0 else 256; _=n;',
        'const bytes=[_]u8{46,120,46}; const name:[]const u8=bytes[1..2]; const n:u8=if(@hasDecl(N,name))0 else 256; _=n;',
        'const empty=[_]u8{}; const n:u8=if(!@hasDecl(N,&empty))0 else 256; _=n;',
        'const name:[]const u8="x"; const n:u8=if(@hasDecl(N,name))0 else 256; _=n;',
        'const Alias=N; const n:u8=if(@hasDecl(Alias,"bad"))0 else 256; _=n;',
        'const Local=struct { pub const local:u8=256; }; const n:u8=if(@hasDecl(Local,"local"))0 else 256; _=n;',
        'const Empty=struct {}; const n:u8=if(!@hasDecl(Empty,"x"))0 else 256; _=n;',
        'const n:bool=@hasDecl(N,"counter"); const p:*const bool=&n; _=p;',
    ):
        yield prefix + 'pub fn main() void { '+body+' }'
    for name_decl, expression in (
        ('const name="x";', 'name'),
        ('const bytes=[_]u8{120};', '&bytes'),
        ('const bytes=[_]u8{46,120,46};', 'bytes[1..2]'),
    ):
        yield prefix + name_decl + f'pub fn main() void {{ const n:u8=if(@hasDecl(N,{expression}))0 else 256; _=n; }}'
    for declaration in ('const private:u8=256;', 'var private:u8=1;', 'fn private() u8 { return 256; }'):
        yield ('const P=struct { '+declaration+' pub fn check() bool { return @hasDecl(@This(),"private"); } }; '
               'pub fn main() void { const n:u8=if(!@hasDecl(P,"private") and !P.check())0 else 256; _=n; }')
    # Names do not inherit parents, and presence must not resolve invalid values.
    yield 'const N=struct { pub const a=@hasDecl(@This(),"a"); }; pub fn main() void { const n:u8=if(N.a)0 else 256; _=n; }'
    yield 'const N=struct { pub const x:u8=x; }; pub fn main() void { const n:u8=if(@hasDecl(N,"x"))0 else 256; _=n; }'
    yield 'pub const x:u8=256; pub fn main() void { const n:u8=if(@hasDecl(@This(),"x"))0 else 256; _=n; }'
    for name in ('"x"', '""', '3'):
        test = f'@hasDecl(anyopaque,{name})'
        yield f'pub fn main() void {{ const n:u8=if(!{test})0 else 256; _=n; }}'
    for base in ('u8','bool','void','type','usize','u0','i129','comptime_int','comptime_float',
                 '[3]u8','[0]u8','*u8','[]const u8','[*]u8','fn() void','anyerror','noreturn','f32','f64'):
        yield f'pub fn main() void {{ _=@hasDecl({base},"x"); }}'
    for base in ('3','true','null','@as(type,undefined)'):
        for name in ('"x"','3','undefined'):
            yield f'pub fn main() void {{ _=@hasDecl({base},{name}); }}'
    for base in ('N','u8'):
        for name in ('3','true','null','u8','undefined','@as([]const u8,undefined)',
                     '@as([]const u16,undefined)','[_]u8{120}','.{120}'):
            yield prefix + f'pub fn main() void {{ _=@hasDecl({base},{name}); }}'
    for body in (
        'var bytes=[_]u8{120}; _=&bytes; _=@hasDecl(N,&bytes);',
        'var bytes=[_]u8{}; _=&bytes; _=@hasDecl(N,&bytes);',
        'const p:*const [0]u8=undefined; _=@hasDecl(N,p);',
        'const p:*const [1]u8=undefined; _=@hasDecl(N,p);',
        'const p:[*]const u8=undefined; _=@hasDecl(N,p[0..0]);',
        'const p:[*]const u8=undefined; _=@hasDecl(N,p[0..1]);',
        'const p:[*]const u8=@ptrFromInt(123); _=@hasDecl(N,p[0..1]);',
        'const p:*allowzero const [0]u8=@ptrFromInt(0); _=@hasDecl(N,p);',
        'const p:[*]allowzero const u8=@ptrFromInt(0); _=@hasDecl(N,p[0..0]);',
    ):
        yield prefix + 'pub fn main() void { '+body+' }'
    for source in (
        'fn run(name:[]const u8) bool { return @hasDecl(N,name); } pub fn main() void { _=run("x"); }',
        'fn run(name:*const [1]u8) bool { return @hasDecl(N,name); } pub fn main() void { _=run("x"); }',
        'var bytes:[1]u8=.{120}; pub fn main() void { _=@hasDecl(N,&bytes); }',
        'var bytes:[0]u8=.{}; pub fn main() void { _=@hasDecl(N,&bytes); }',
        'var name:[]const u8="x"; pub fn main() void { _=@hasDecl(N,name); }',
    ):
        yield prefix + source
    # The reference consumes exactly one final NUL when interning a name.
    for name, present in (
        ('"x\\x00"', True), ('".x\\x00."[1..3]', True),
        ('&[_]u8{120,0}', True), ('&[_:0]u8{120,0}', True),
        ('"x\\x00\\x00"', False), ('"x\\x00suffix\\x00"', False),
        ('&[_]u8{120,0,0}', False),
    ):
        test = f'@hasDecl(N,{name})'
        if not present:
            test = '!' + test
        yield prefix + f'pub fn main() void {{ const n:u8=if({test})0 else 256; _=n; }}'
    for args in ('','N','N,"x",3'):
        yield prefix + f'pub fn main() void {{ _=@hasDecl({args}); }}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-has-decl-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_has_decl case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_has_decl case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'hasDecl: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
