#!/usr/bin/env python3
"""Compare supported @inComptime contexts and diagnostics with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    for expr, expected in (('@inComptime()', 'false'), ('!@inComptime()', 'true'),
                           ('(@inComptime())', 'false'),
                           ('if(true) @inComptime() else true', 'false'),
                           ('@inComptime() or @inComptime()', 'false')):
        yield ('pub fn main() void { '
               f'const n:u8=if(({expr})=={expected})0 else 256; _=n; }}')
    forced = (
        'const x=$; pub fn main() void { _=x; }',
        'var x=$; pub fn main() void { _=&x; }',
        'const x=$; pub fn main() void {}',
        'pub fn main() void { const x:if($)u8 else u16=0; _=x; }',
        'pub fn main() void { _=@sizeOf(if($)u8 else u16); }',
        'pub fn main() void { _=@alignOf(if($)u8 else u16); }',
        'pub fn main() void { _=@bitSizeOf(if($)u8 else u16); }',
        'pub fn main() void { _=@as(if($)u8 else u16,0); }',
        'pub fn main() void { _=[if($)1 else 2]u8; }',
        'pub fn main() void { _=[2]if($)u8 else u16; }',
        'pub fn main() void { _=*if($)u8 else u16; }',
        'pub fn main() void { _=[2:if($)0 else 1]u8; }',
        'pub fn main() void { _=[:if($)0 else 1]u8; }',
        'pub fn main() void { _=[*:if($)0 else 1]u8; }',
        'pub fn main() void { _=*align(if($)4 else 8)u8; }',
        'pub fn main() void { var x:u8 align(if($)4 else 8)=0; _=&x; }',
        'pub fn main() void { _=@hasField([]u8,if($)"ptr" else "len"); }',
        'pub fn main() void { _=@hasDecl(struct {},if($)"a" else "b"); }',
        'pub fn main() void { _=@hasField(if($)[]u8 else []u16,"len"); }',
        'pub fn main() void { const x=[_]u8{1}; _=@field(x,if($)"len" else "len"); }',
        'pub fn main() void { comptime var x=$; _=&x; }',
        'fn f() if($)u8 else u16 { return 0; } pub fn main() void { _=f(); }',
        'fn f(x:if($)u8 else u16) void { _=x; } pub fn main() void { f(0); }',
        'pub fn main() void { _=@TypeOf(@as(if($)u8 else u16,0)); }',
        'pub fn main() void { _=@TypeOf([if($)1 else 2]u8); }',
        'const N=struct { const x=$; }; pub fn main() void { _=N; }',
    )
    for template in forced:
        for expr in ('@inComptime()', '!@inComptime()',
                     'if(true) @inComptime() else false'):
            yield template.replace('$', expr)
    query = ('blk: { const n:u8=if(!@inComptime())0 else 256; '
             '_=n; break :blk false; }')
    for prefix, expr in (('', '@inComptime()'), ('', query),
                         ('const T=@TypeOf('+query+'); ', query)):
        yield prefix + 'pub fn main() void { _=@TypeOf('+expr+'); }'
    for source in (
        'pub fn main() void { _=@as(bool,@inComptime()); }',
        'pub fn main() void { const a=[_]bool{@inComptime(),true}; _=a; }',
        'pub fn main() void { var a=[_]bool{true,false}; const s=a[0..1:@inComptime()]; _=s; }',
        'pub fn main() void { const T=@TypeOf(@inComptime()); const n:u8=if(T==bool)0 else 256; _=n; }',
        'pub fn main() void { const a:u8=if(@inComptime())256 else 0; _=a; }',
        'pub fn main() void { _=@inComptime(1); }',
        'pub fn main() void { _=@inComptime(1,2); }',
        'const x=@inComptime(1); pub fn main() void { _=x; }',
    ):
        yield source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-in-comptime-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked, source in enumerate(cases(), 1):
            path.write_text(source)
            a = run_compiler([args.szc, SZC_MODE, str(path)], source,
                             f'diff_in_comptime case {checked} szc', timeout=120)
            b = run_compiler([args.zig, 'build-exe', str(path), '-fno-emit-bin', '-j2'], source,
                             f'diff_in_comptime case {checked} zig', timeout=120)
            error = primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n'+source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'inComptime: {checked} matched Zig 0.17.0; {diagnostics} exact primary diagnostics', flush=True)


if __name__ == '__main__':
    main()
