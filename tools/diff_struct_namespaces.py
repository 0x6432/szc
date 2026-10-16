#!/usr/bin/env python3
"""Compare fieldless struct namespaces and lexical @This() with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    prefix = ('const Root=@This(); const Module=struct { const Self=@This(); '
              'const Byte=u8; const value:Byte=17; const len:usize=3; '
              'var counter:Byte=1; const aligned:Byte align(32)=23; '
              'const lazy_bad:Byte=256; fn add(n:Byte) Byte { counter+=n; return counter; } '
              'fn check() bool { return @This()==Self and Self==Module; } '
              'const Child=struct { const Self=@This(); const value:Byte=29; '
              'fn check() bool { return @This()==@This().Self and @This().Self!=Module; } }; }; ')
    for expression in ('Root==@This()', 'Module.Self==Module', 'Module.value==17',
                       'Module.len==3', 'Module.Child.value==29',
                       'Module.Child.Self!=Module', '@sizeOf(Module.Byte)==1',
                       'Module.Child.Self==Module.Child', '&Module.value==&Module.value',
                       '@intFromPtr(&Module.aligned)&31==0'):
        yield prefix + f'pub fn main() void {{ const n:u8=if({expression})0 else 256; _=n; }}'
    for body in ('_=Module.check(); _=Module.Child.check();',
                 'Module.counter=3; Module.counter+=2; _=Module.add(7);',
                 'const p=&Module.counter; p.*=7;',
                 'const callback:*const fn(u8) u8=&Module.add; _=callback(2);',
                 'const alias=Module.add; _=alias(3);',
                 'const pointer=&Module; _=pointer.value; _=pointer.Child.value;',
                 'const Alias=Module.Child; const p=&Alias; _=p.value;',
                 'const n:Module.Byte=23; _=n;',
                 '{ const T=@This(); const n:u8=if(T==Root)0 else 256; _=n; }'):
        yield prefix + 'pub fn main() void { '+body+' }'
    for field in ('missing', 'counter', 'len', 'ptr'):
        # Namespace lookup never searches parents for an explicitly qualified member.
        yield prefix + f'pub fn main() void {{ _=Module.Child.{field}; }}'
    for base in ('Module', 'Root', '@This()', 'Module.Child', 'Module.Self'):
        yield prefix + f'pub fn main() void {{ _={base}.absent; }}'
    for base in ('u8', 'bool', 'void', 'type', 'usize', '[3]u8', '*u8', '[]u8'):
        yield f'const T={base}; pub fn main() void {{ _=T.absent; }}'
    for expression in ('@This(1)', '@This(u8,u16)'):
        yield f'pub fn main() void {{ _={expression}; }}'
    for leaf in ('u8', 'bool', 'usize', '*u8', '[]u8'):
        yield (f'const N=struct {{ const T={leaf}; }}; '
               'pub fn main() void { const Alias=N.T; _=@sizeOf(Alias); }')
    for name in ('value','len','ptr','item'):
        for use in (f'_={name};', f'_=&{name};', f'{name}=4;'):
            yield (f'var {name}:u8=1; const N=struct {{ var {name}:u8=2; '
                   f'fn test_it() void {{ {use} }} }}; pub fn main() void {{ N.test_it(); }}')
        yield (f'const {name}:u8=1; const N=struct {{ const {name}:u8=2; '
               f'fn read() u8 {{ return @This().{name}; }} }}; '
               'pub fn main() void { _=N.read(); }')
    for body in ('_=N.value;', 'const p=&N.value; _=p;', 'N.value=3;'):
        yield 'const N=struct { const value:u8=1; }; pub fn main() void { '+body+' }'
    for use in ('_=x;', '_=&x;', 'x=3;'):
        yield ('pub fn main() void { var x:u8=7; _=&x; const N=struct { '
               'fn run() void { '+use+' } }; N.run(); }')
    for source in (
        'const N=struct { const x:u8=x; }; pub fn main() void { _=N.x; }',
        'const N=struct { const x:u8=N.x; }; pub fn main() void { _=N.x; }',
        'const N=struct { const x:u8=256; }; pub fn main() void { _=N; }',
        'const N=struct { const x:u8=256; }; pub fn main() void { _=N.x; }',
        'const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { _=N; }',
        'const N=struct { export fn exported() u8 { return 256; } }; pub fn main() void {}',
        'const N=struct { export fn exported() u8 { return 256; } }; pub fn main() void { _=N; }',
        'const N=struct { export fn exported() u8 { return @This().value; } const value:u8=7; }; pub fn main() void { _=N; }',
        'const Root=@This(); fn same() bool { return @This()==Root; } const N=struct { const alias=same; }; pub fn main() void { _=N.alias(); }',
        'const A=struct {}; const B=struct {}; pub fn main() void { const n:u8=if(A!=B)0 else 256; _=n; }',
        'pub fn main() void { const Local=struct { const Self=@This(); fn same() bool { return Self==@This(); } }; _=Local.same(); }',
    ):
        yield source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-struct-namespaces-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_struct_namespaces case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_struct_namespaces case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'struct namespaces: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
