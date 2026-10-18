#!/usr/bin/env python3
"""Szc-only NYI gates: intentionally NOT official diagnostic-parity cases."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

CASES = (
    ('packed-offset', 'const S=packed struct { a:u3,b:u4, };', '@bitOffsetOf(S,"b")'),
    ('packed-size', 'const S=packed struct { a:u3,b:u4, };', '@bitSizeOf(S)'),
    ('extern-offset', 'const S=extern struct { a:u8,b:u64, };', '@bitOffsetOf(S,"b")'),
    ('extern-size', 'const S=extern struct { a:u8,b:u64, };', '@bitSizeOf(S)'),
    ('tuple-offset', 'const S=struct { u8,u64, };', '@bitOffsetOf(S,"1")'),
    ('default-offset', 'const S=struct { a:u8=1, };', '@bitOffsetOf(S,"a")'),
    ('comptime-offset', 'const S=struct { comptime a:u8=1, };', '@bitOffsetOf(S,"a")'),
    ('align-offset', 'const S=struct { a:u8 align(16), };', '@bitOffsetOf(S,"a")'),
    # AstGen/irgen explicit alignment rejection precedes the no-representation path.
    ('align-size-irgen-gate', 'const S=struct { a:u8 align(16), };', '@bitSizeOf(S)'),
)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    args=parser.parse_args()
    failures=[]
    with tempfile.TemporaryDirectory(prefix='szc-struct-bit-layout-nyi-') as directory:
        path=Path(directory)/'test.zig'
        for index,(label,prefix,expression) in enumerate(CASES,1):
            source=prefix+' pub fn main() void { _='+expression+'; }'
            path.write_text(source)
            result=run_compiler([args.szc,'emit-qbe',str(path)],source,f'struct bit layout NYI case {index} szc',timeout=120)
            error=primary_error(result.stderr)
            if result.returncode!=1 or error is None or 'not supported by szc yet:' not in error:
                failures.append(label+': '+str(error))
    print(f'struct bit layout NYI: {len(CASES)} cases; {len(failures)} failures')
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
