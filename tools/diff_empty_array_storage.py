#!/usr/bin/env python3
"""Compare empty runtime array storage and internal ABI with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    for leaf in ('type', 'comptime_int', 'comptime_float', 'fn() void', 'noreturn',
                 'u65', 'u128', 'i128', 'u256', 'f32', 'f64'):
        for ty in (f'[0]{leaf}', f'[2][0]{leaf}', f'[0][2]{leaf}', f'[2][3][0]{leaf}'):
            yield (f'fn identity(a:{ty}) {ty} {{ return a; }} '
                   f'pub fn main() void {{ var a:{ty}=undefined; _=&a; const b=identity(a); _=b; }}\n')
            yield (f'var a:{ty}=undefined; fn identity(p:*{ty}) void {{ _=p; }} '
                   f'pub fn main() void {{ identity(&a); }}\n')
    for leaf in ('type','u128','f32','fn() void'):
        yield (f'fn fill(d:[][0]{leaf}) void {{ @memset(d,.{{}}); }} '
               f'pub fn main() void {{ var a:[3][0]{leaf}=undefined; fill(&a); '
               '@memcpy(a[0..2],a[1..3]); }\n')
    for shape in ('[0]type', '[2][0]type', '[0]u128', '[2][0]f32'):
        yield (f'pub fn main() void {{ const a:{shape}=undefined; _=&a; }}\n')

    for leaf in ('type', 'u128', 'f32', 'fn() void'):
        yield f'pub fn main() void {{ const a:[0]{leaf}=.{{}}; _=a[0]; }}\n'
        yield (f'pub fn main() void {{ const a:[2][0]{leaf}=undefined; '
               '@memset(&a,.{}); }\n')
        yield (f'pub fn main() void {{ var a:[2][0]{leaf}=undefined; '
               f'const b:[1][0]{leaf}=.{{.{{}}}}; @memcpy(&a,&b); }}\n')
    for index in ('0','@as(usize,undefined)'):
        yield (f'pub fn main() void {{ const a:[0]type=.{{}}; _=a[{index}]; }}\n')
        yield (f'pub fn main() void {{ const p:*[0]type=undefined; _=p[{index}]; }}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-empty-array-storage-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_empty_array_storage case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_empty_array_storage case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'empty array storage: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
