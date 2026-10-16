#!/usr/bin/env python3
"""Compare canonical quoted identifier names and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    spellings = ('answer', '@"answer"', '@"\\x61nswer"', '@"\\u{61}nswer"')
    for declaration in spellings:
        for reference in spellings:
            yield f'const {declaration}:u8=7; pub fn main() void {{ _={reference}; }}'
            yield (f'const N=struct {{ const {declaration}:u8=7; }}; '
                   f'pub fn main() void {{ _=N.{reference}; }}')
    for declaration, reference in (('@"é"','@"\\u{e9}"'), ('@"\\xc3\\xa9"','@"é"'),
                                    ('@"space name"','@"space\\x20name"'), ('@"_"','@"\\x5f"'),
                                    ('@"return"','@"\\x72eturn"')):
        yield f'const {declaration}:u8=7; pub fn main() void {{ _={reference}; }}'
        yield (f'const N=struct {{ var {declaration}:u8=7; }}; '
               f'pub fn main() void {{ const p=&N.{reference}; p.*+=1; }}')
    for spelling in spellings[1:]:
        yield f'pub fn main() void {{ const {spelling}:u8=7; _=answer; }}'
        yield f'fn {spelling}(n:u8) u8 {{ return n; }} pub fn main() void {{ _=answer(7); }}'
        yield f'const answer:u8=1; const {spelling}:u8=2; pub fn main() void {{ _=answer; }}'
        yield f'const answer:u8=1; pub fn main() void {{ const {spelling}:u8=2; _=answer; }}'
    for spelling in ('@"u8"','@"true"','@"false"','@"undefined"','@"_"','@"\\x6dissing"'):
        yield f'pub fn main() void {{ _={spelling}; }}'
    for primitive in ('u8','usize','bool','u17'):
        yield (f'const @"{primitive}":u8=7; pub fn main() void {{ '
               f'const n:{primitive}=undefined; _=n; _=@"{primitive}"; _=@sizeOf({primitive}); }}')
        yield (f'var @"{primitive}":u8=7; pub fn main() void {{ '
               f'const p:*const type=&{primitive}; _=p; @"{primitive}"+=1; }}')
        yield (f'var @"{primitive}":u8=7; pub fn main() void {{ {primitive}=3; }}')
    for spelling in ('@""','@"\\x00"','@"a\\x00b"','@"\\u{0}"','@"\\q"','@"\\u{d800}"'):
        yield f'const {spelling}:u8=7; pub fn main() void {{}}'
        yield f'pub fn main() void {{ _=@This().{spelling}; }}'
    yield 'pub fn @"\\x6dain"() void {}'
    yield 'pub fn main() void { @"\\x6cab": { break :lab; } }'
    yield 'pub fn main() void { lab: { break :@"\\x6cab"; } }'
    yield 'fn f(@"\\x6e":u8) u8 { return n; } pub fn main() void { _=f(7); }'
    yield 'const value:u8=7; const N=struct { const @"\\x76alue":u8=8; fn f() u8 { return value; } }; pub fn main() void { _=N.f(); }'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-quoted-identifiers-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_quoted_identifiers case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_quoted_identifiers case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n' + source + a.stderr.decode(errors='replace')
                                 + b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'quoted identifiers: {checked} matched Zig 0.17.0; '
              f'{diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
