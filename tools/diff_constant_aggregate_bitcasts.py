#!/usr/bin/env python3
"""Compare constant packed array bit casts with Zig, independently of ABI stride."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler


def checked(body, condition):
    return ('pub fn main() void { '+body+
            f' const check:u8=if({condition})0 else 256; _=check; }}\n')


def cases():
    for width in (1,5,8,16,31,32,33,64,65,128):
        mask = (1 << width)-1
        for length in (0,1,2,3,5):
            for signed in (False,True):
                child = ('i' if signed else 'u')+str(width)
                raw = [((i+1)*17 ^ mask) & mask for i in range(length)]
                values = [v-(1<<width) if signed and v >= 1<<(width-1) else v for v in raw]
                array = f'[{length}]{child}'
                init = '.{'+','.join(map(str,values))+'}'
                packed = sum(v << (i*width) for i,v in enumerate(raw))
                bits = length*width
                for destination_signed in (False,True):
                    if bits == 0 and destination_signed:
                        continue
                    destination = ('i' if destination_signed else 'u')+str(bits)
                    expected = packed-(1<<bits) if destination_signed and packed >= 1<<(bits-1) else packed
                    yield checked(f'const n:{destination}=@bitCast(@as({array},{init}));',f'n=={expected}')
                if length == 0:
                    yield 'pub fn main() void { const n:[0]'+child+'=@bitCast(@as(u0,0)); _=n; }\n'
                else:
                    condition = ' and '.join(f'n[{i}]=={v}' for i,v in enumerate(values))
                    yield checked(f'const n:{array}=@bitCast(@as(u{bits},{packed}));',condition)
    for child,width,sentinel in (('u5',5,17),('u8',8,17),('i5',5,-1),('bool',1,1)):
        for length in (0,1,2,3):
            scalar = 'true' if child == 'bool' else str(sentinel)
            values = [i & ((1<<width)-1) for i in range(length)]
            values = [v-(1<<width) if child.startswith('i') and v>=1<<(width-1) else v for v in values]
            texts = [('true' if v else 'false') if child=='bool' else str(v) for v in values]
            array=f'[{length}:{scalar}]{child}'
            init='.{'+','.join(texts)+'}'
            bits=(length+1)*width
            encoded=sum((v % (1<<width)) << (i*width) for i,v in enumerate(values+[sentinel]))
            yield checked(f'const n:u{bits}=@bitCast(@as({array},{init}));',f'n=={encoded}')
            encoded ^= 1 << (length*width)
            conditions=[f'n[{i}]=={v}' for i,v in enumerate(texts)]+[f'n[{length}]=={scalar}']
            yield checked(f'const n:{array}=@bitCast(@as(u{bits},{encoded}));',' and '.join(conditions))
    for source in ('@as([2]u8,.{1,undefined})','@as([2]u8,.{undefined,undefined})',
                   '@as([2]u8,.{undefined,2})','@as([2]u8,undefined)'):
        yield 'pub fn main() void { const n:u16=@bitCast('+source+'); _=n; }\n'
    for body,condition in (
        ('const n:u16=@bitCast(@as([2]u8,.{undefined,undefined}));','n==0'),
        ('const n:u16=@bitCast(@as([2]u8,.{1,undefined}));','n==1'),
        ('const n:u20=@bitCast(@as([2][2]u5,.{.{1,2},.{3,4}}));','n==134209'),
        ('const n:[2][2]u5=@bitCast(@as(u20,134209));','n[0][0]==1 and n[0][1]==2 and n[1][0]==3 and n[1][1]==4'),
        ('const n:[2]u0=@bitCast(@as(void,{}));','n[0]==0 and n[1]==0'),
        ('const n:[2]u0=@bitCast(@as([2]u0,.{undefined,undefined}));','n[0]==0 and n[1]==0'),
        ('const n:[2]u0=@bitCast(@as(u0,undefined));','n[0]==0 and n[1]==0'),
        ('const n:[2]u0=@bitCast(@as([2]u0,undefined));','n[0]==0 and n[1]==0'),
        ('const n:u24=@bitCast(@as([2:17]u8,.{undefined,undefined}));','n==0x110000'),
        ('const n:u32=@bitCast(@as([2][2]u8,.{.{undefined,undefined},.{undefined,undefined}}));','n==0'),
        ('const n:u48=@bitCast(@as([2][2:17]u8,.{undefined,.{1,2}}));','n==0x110201000000'),
        ('const n:u48=@bitCast(@as([2][2:17]u8,.{.{undefined,undefined},.{1,2}}));','n==0x110201110000'),
    ):
        yield checked(body,condition)
    for source,destination in (('[2]u8','u8'),('[2]bool','u8'),('[2:17]u8','u16'),('[0]*u8','[0]u8'),('[0]u8','[0]*u8')):
        yield f'pub fn main() void {{ const n:{destination}=@bitCast(@as({source},undefined)); _=n; }}\n'
    for expression in ('@as([2:17]u8,.{undefined,undefined})',
                       '@as([2][2]u8,.{.{undefined,undefined},.{undefined,undefined}})',
                       '@as([2][2:17]u8,.{undefined,.{1,2}})'):
        yield 'pub fn main() void { const n = @as(u'+('48' if '[2][2:17]' in expression else '32' if '[2][2]' in expression else '24')+',@bitCast('+expression+')); _=n; }\n'
    yield 'pub fn main() void { const n:[0]u8=@bitCast(@as([0][5000000000]u8,.{})); _=n; }\n'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc');parser.add_argument('zig');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-constant-aggregate-bitcasts-') as directory:
        path=Path(directory)/'test.zig'
        for checked_count,source in enumerate(cases(),1):
            path.write_text(source)
            a=run_compiler([args.szc,'tir',str(path)],source,f'diff_constant_aggregate_bitcasts case {checked_count} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_constant_aggregate_bitcasts case {checked_count} zig',timeout=120)
            if a.returncode != b.returncode:
                raise SystemExit('disagreement:\n'+source+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
        print(f'constant aggregate bit casts: {checked_count} matched Zig 0.17.0',flush=True)


if __name__=='__main__':
    main()
