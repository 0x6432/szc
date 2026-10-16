#!/usr/bin/env python3
"""Compare supported array/slice/fieldless namespace @hasField with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def assertion(test, expected):
    if not expected:
        test = '!' + test
    return f'pub fn main() void {{ const n:u8=if({test})0 else 256; _=n; }}'


def cases():
    prefix = ('const N=struct { pub const len:u8=256; const ptr:u8=256; '
              'pub fn bad() u8 { return 256; } const Self=@This(); '
              'const Child=struct {}; }; ')
    names = ('len', 'ptr', 'bad', '', 'Len', 'lenX', 'len\\x00',
             'len\\x00tail', 'ptr\\x00tail', '\\x00len')
    for ty in ('[0]u0', '[3]u8', '[3:0]u8', '[2][3]u16', '[0]f32',
               '[]u8', '[]const u8', '[:0]const u8', '[]align(4) volatile u8',
               '[]allowzero u8', 'N', 'N.Child', '@This()', 'struct {}'):
        for name in names:
            expected = (ty.startswith('[') and not ty.startswith('[]') and not ty.startswith('[:')
                        and name.startswith('len') and name in ('len', 'len\\x00', 'len\\x00tail'))
            if ty.startswith('[]') or ty.startswith('[:'):
                expected = name in ('len', 'ptr', 'len\\x00', 'len\\x00tail', 'ptr\\x00tail')
            yield prefix + assertion(f'@hasField({ty},"{name}")', expected)
    for name in ('"len"', '".len."[1..4]', '"l"++"en"', '&[_]u8{108,101,110}',
                 '&.{108,101,110}', '&[_:0]u8{108,101,110}', '"len\\x00\\x00"'):
        yield assertion(f'@hasField([2]u8,{name})', True)
    for source in (
        'const name:[]const u8="ptr"; pub fn main() void { const n:u8=if(@hasField([]u8,name))0 else 256; _=n; }',
        'pub fn main() void { const bytes=[_]u8{108,101,110}; const n:u8=if(@hasField([2]u8,&bytes))0 else 256; _=n; }',
        'const N=struct { pub const check=@hasField(@This(),"check"); }; pub fn main() void { const n:u8=if(!N.check)0 else 256; _=n; }',
        'pub fn main() void { const b=@hasField([3]u8,"len"); const p:*const bool=&b; _=p; }',
        'pub fn main() void { const T=[if(@hasField([3]u8,"len"))1 else 2]u8; const a:T=.{7}; _=a; }',
    ):
        yield source
    for ty in ('*u8', '*[3]u8', '*[]u8', '[*]u8', '*const type', 'u8', 'u0', 'i129',
               'bool', 'void', 'type', 'anyopaque', 'anyerror', 'noreturn', 'fn() void', 'f32'):
        for name in ('"len"', '3'):
            yield f'pub fn main() void {{ _=@hasField({ty},{name}); }}'
    for ty in ('3', 'true', 'null', '@as(type,undefined)'):
        for name in ('"len"', '3', 'undefined'):
            yield f'pub fn main() void {{ _=@hasField({ty},{name}); }}'
    for ty in ('[3]u8', 'u8'):
        for name in ('3', 'true', 'null', 'u8', 'undefined', '@as([]const u16,undefined)',
                     '[_]u8{108,101,110}', '.{108,101,110}'):
            yield f'pub fn main() void {{ _=@hasField({ty},{name}); }}'
    for body in (
        'var bytes=[_]u8{108,101,110}; _=&bytes; _=@hasField([3]u8,&bytes);',
        'const p:*const [0]u8=undefined; _=@hasField([3]u8,p);',
        'const p:*allowzero const [0]u8=@ptrFromInt(0); _=@hasField([3]u8,p);',
    ):
        yield 'pub fn main() void { '+body+' }'
    yield 'fn run(name:[]const u8) bool { return @hasField([3]u8,name); } pub fn main() void { _=run("len"); }'
    yield 'var name:[]const u8="len"; pub fn main() void { _=@hasField([3]u8,name); }'
    for args in ('', '[3]u8', '[3]u8,"len",3'):
        yield f'pub fn main() void {{ _=@hasField({args}); }}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-has-field-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_has_field case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_has_field case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'hasField: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
