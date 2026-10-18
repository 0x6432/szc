#!/usr/bin/env python3
"""Bounded ordinary @bitOffsetOf / no-representation @bitSizeOf differential.

Copy alongside compiler_process.py and diff_thin_pointer_casts.py. Reference-only:
python3 diff_struct_bit_layout.py - /path/to/zig --reference-only --report ref.json
Numeric bit offsets were captured from official Zig 0.17.0, not a Python sort.
Packed/extern/tuple/default/comptime/aligned offset queries are separate NYI gates.
"""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = 'emit-qbe'

@dataclass
class Case:
    label: str
    source: str
    lane: str = 'positive'

# Filled by freeze_oracle.py from captured official compile-log output.
LAYOUTS = [{'label': 'perm3/0',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u8,f1:u64,f2:u16,}; ',
  'fields': ['f0', 'f1', 'f2'],
  'values': [16, 8, 80, 0, 64]},
 {'label': 'perm3/5',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u16,f1:u64,f2:u8,}; ',
  'fields': ['f0', 'f1', 'f2'],
  'values': [16, 8, 64, 0, 80]},
 {'label': 'zero/0',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{a:u8,z:void,empty:[0]u64,bits:u0,b:u64,}; ',
  'fields': ['a', 'z', 'empty', 'bits', 'b'],
  'values': [16, 8, 64, 72, 0, 72, 0]},
 {'label': 'zero/1',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{z:void,empty:[0]u64,bits:u0,b:u64,a:u8,}; ',
  'fields': ['z', 'empty', 'bits', 'b', 'a'],
  'values': [16, 8, 64, 0, 64, 0, 64]},
 {'label': 'zero/2',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{empty:[0]u64,bits:u0,b:u64,a:u8,z:void,}; ',
  'fields': ['empty', 'bits', 'b', 'a', 'z'],
  'values': [16, 8, 0, 64, 0, 64, 72]},
 {'label': 'zero/3',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{bits:u0,b:u64,a:u8,z:void,empty:[0]u64,}; ',
  'fields': ['bits', 'b', 'a', 'z', 'empty'],
  'values': [16, 8, 64, 0, 64, 72, 64]},
 {'label': 'wide/0',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{a:u513,b:i129,c:u24,d:u33,e:[3]u8,f:bool,}; ',
  'fields': ['a', 'b', 'c', 'd', 'e', 'f'],
  'values': [128, 16, 0, 640, 960, 896, 992, 1016]},
 {'label': 'wide/1',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{f:bool,e:[3]u8,d:u33,c:u24,b:i129,a:u513,}; ',
  'fields': ['f', 'e', 'd', 'c', 'b', 'a'],
  'values': [128, 16, 992, 1000, 896, 960, 0, 256]},
 {'label': 'wide/2',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{c:u24,d:u33,e:[3]u8,f:bool,a:u513,b:i129,}; ',
  'fields': ['c', 'd', 'e', 'f', 'a', 'b'],
  'values': [128, 16, 960, 896, 992, 1016, 0, 640]},
 {'label': 'many/25/shuffle',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{f1:u64,f23:bool,f24:u8,f14:u24,f6:u24,f5:[0]u32,f2:u16,f21:[0]u32,f18:u16,f11:u65,f16:u8,f4:void,f7:bool,f20:void,f9:u64,f3:u65,f8:u8,f0:u8,f15:bool,f17:u64,f19:u65,f10:u16,f22:u24,f12:void,f13:[0]u32,}; ',
  'fields': ['f1',
             'f23',
             'f24',
             'f14',
             'f6',
             'f5',
             'f2',
             'f21',
             'f18',
             'f11',
             'f16',
             'f4',
             'f7',
             'f20',
             'f9',
             'f3',
             'f8',
             'f0',
             'f15',
             'f17',
             'f19',
             'f10',
             'f22',
             'f12',
             'f13'],
  'values': [112,
             16,
             448,
             768,
             760,
             608,
             640,
             672,
             672,
             672,
             688,
             0,
             752,
             752,
             720,
             752,
             512,
             256,
             728,
             736,
             744,
             384,
             128,
             704,
             576,
             768,
             576]},
 {'label': 'many/51/shuffle',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{f17:u64,f2:u16,f42:u16,f34:u16,f1:u64,f10:u16,f4:void,f32:u8,f50:u16,f8:u8,f47:bool,f9:u64,f3:u65,f37:[0]u32,f30:u24,f27:u65,f15:bool,f38:u24,f5:[0]u32,f20:void,f12:void,f39:bool,f14:u24,f45:[0]u32,f13:[0]u32,f26:u16,f19:u65,f49:u64,f36:void,f40:u8,f0:u8,f21:[0]u32,f48:u8,f31:bool,f11:u65,f43:u65,f33:u64,f28:void,f25:u64,f7:bool,f22:u24,f6:u24,f23:bool,f29:[0]u32,f46:u24,f16:u8,f24:u8,f41:u64,f44:void,f18:u16,f35:u65,}; ',
  'fields': ['f17',
             'f2',
             'f42',
             'f34',
             'f1',
             'f10',
             'f4',
             'f32',
             'f50',
             'f8',
             'f47',
             'f9',
             'f3',
             'f37',
             'f30',
             'f27',
             'f15',
             'f38',
             'f5',
             'f20',
             'f12',
             'f39',
             'f14',
             'f45',
             'f13',
             'f26',
             'f19',
             'f49',
             'f36',
             'f40',
             'f0',
             'f21',
             'f48',
             'f31',
             'f11',
             'f43',
             'f33',
             'f28',
             'f25',
             'f7',
             'f22',
             'f6',
             'f23',
             'f29',
             'f46',
             'f16',
             'f24',
             'f41',
             'f44',
             'f18',
             'f35'],
  'values': [208,
             16,
             1088,
             1424,
             1472,
             1504,
             1024,
             1488,
             1608,
             1544,
             1408,
             1616,
             1608,
             832,
             640,
             1248,
             1248,
             0,
             1584,
             1312,
             1344,
             1584,
             1520,
             1536,
             1376,
             1408,
             1376,
             1456,
             512,
             1152,
             1536,
             1560,
             1528,
             1408,
             1568,
             1520,
             384,
             256,
             768,
             1544,
             960,
             1552,
             1216,
             1344,
             1576,
             1344,
             1280,
             1592,
             1600,
             896,
             1608,
             1440,
             128]},
 {'label': 'many/64/shuffle',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{f34:u16,f62:u24,f26:u16,f1:u64,f7:bool,f56:u8,f63:bool,f60:void,f28:void,f44:void,f50:u16,f5:[0]u32,f33:u64,f51:u65,f38:u24,f39:bool,f6:u24,f16:u8,f9:u64,f30:u24,f57:u64,f61:[0]u32,f36:void,f25:u64,f15:bool,f53:[0]u32,f58:u16,f21:[0]u32,f41:u64,f11:u65,f27:u65,f59:u65,f18:u16,f48:u8,f20:void,f22:u24,f37:[0]u32,f17:u64,f2:u16,f0:u8,f45:[0]u32,f42:u16,f54:u24,f10:u16,f32:u8,f24:u8,f35:u65,f12:void,f8:u8,f23:bool,f40:u8,f3:u65,f55:bool,f52:void,f29:[0]u32,f4:void,f14:u24,f43:u65,f46:u24,f47:bool,f49:u64,f13:[0]u32,f31:bool,f19:u65,}; ',
  'fields': ['f34',
             'f62',
             'f26',
             'f1',
             'f7',
             'f56',
             'f63',
             'f60',
             'f28',
             'f44',
             'f50',
             'f5',
             'f33',
             'f51',
             'f38',
             'f39',
             'f6',
             'f16',
             'f9',
             'f30',
             'f57',
             'f61',
             'f36',
             'f25',
             'f15',
             'f53',
             'f58',
             'f21',
             'f41',
             'f11',
             'f27',
             'f59',
             'f18',
             'f48',
             'f20',
             'f22',
             'f37',
             'f17',
             'f2',
             'f0',
             'f45',
             'f42',
             'f54',
             'f10',
             'f32',
             'f24',
             'f35',
             'f12',
             'f8',
             'f23',
             'f40',
             'f3',
             'f55',
             'f52',
             'f29',
             'f4',
             'f14',
             'f43',
             'f46',
             'f47',
             'f49',
             'f13',
             'f31',
             'f19'],
  'values': [256,
             16,
             1824,
             1760,
             1856,
             1344,
             2040,
             2024,
             2016,
             2008,
             2008,
             2008,
             1840,
             1632,
             1088,
             768,
             1632,
             1920,
             1664,
             2000,
             1216,
             1696,
             1024,
             1760,
             1992,
             1152,
             1960,
             1760,
             1888,
             1760,
             1408,
             256,
             896,
             128,
             1792,
             1936,
             1936,
             1536,
             1760,
             1472,
             1872,
             1928,
             1760,
             1904,
             1728,
             1808,
             1944,
             1952,
             384,
             1968,
             1968,
             1976,
             1984,
             640,
             1992,
             2000,
             1696,
             2008,
             1600,
             0,
             1568,
             2008,
             1280,
             1760,
             2032,
             512]},
 {'label': 'nested',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct '
            '{a:u8,inner:Inner,array:[3]Inner,matrix:[2][3]u16,z:void,}; ',
  'fields': ['a', 'inner', 'array', 'matrix', 'z'],
  'values': [48, 4, 352, 0, 64, 256, 360]},
 {'label': 'self-pointer',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct {tag:u8,next:*const '
            '@This(),children:[]@This(),}; ',
  'fields': ['tag', 'next', 'children'],
  'values': [32, 8, 192, 0, 64]},
 {'label': 'all-zero',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct {v:void,u:u0,a:[0]u64,}; ',
  'fields': ['v', 'u', 'a'],
  'values': [0, 8, 0, 0, 0]},
 {'label': 'u32-size-limit',
  'prefix': 'const Inner=struct { byte:u8, word:u32, }; const S=struct {a:[4294967294]u8,b:u8,}; ',
  'fields': ['a', 'b'],
  'values': [4294967295, 1, 0, 34359738352]},
 {'label': 'zero-offset-nonzero',
  'prefix': 'const S=struct { big:u64, byte:u8, zero:[0]u8, }; ',
  'fields': ['big', 'byte', 'zero'],
  'values': [16, 8, 0, 64, 72]},
 {'label': 'zero-alignment',
  'prefix': 'const S=struct { byte:u8, zero:[0]u128, }; ',
  'fields': ['byte', 'zero'],
  'values': [16, 16, 0, 0]}]

def check(prefix, condition):
    return prefix+'pub fn main() void { const n:u8=if('+condition+')0 else 256; _=n; }'

def query(prefix, expression):
    return prefix+'pub fn main() void { _='+expression+'; }'

def cases():
    for row in LAYOUTS:
        size,align,*offsets=row['values']
        tests=[f'@sizeOf(S)=={size}',f'@alignOf(S)=={align}']
        tests += [f'@bitOffsetOf(S,"{field}")=={offset}' for field,offset in zip(row['fields'],offsets)]
        tests += [f'@bitOffsetOf(S,"{field}")==@offsetOf(S,"{field}")*8' for field in row['fields']]
        tests += [f'@TypeOf(@bitOffsetOf(S,"{row["fields"][-1]}"))==comptime_int']
        yield Case('layout/'+row['label'],check(row['prefix'],' and '.join(tests)))
    base='const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; '
    for label,name in (('plain','"item"'),('one-nul','"item\\x00"'),('concat','"i"++"tem"'),('slice','".item."[1..5]'),('array','&[_]u8{105,116,101,109}')):
        yield Case('name/'+label,check(base,f'@bitOffsetOf(S,{name})==64'))
    for label,identifier,name in (('space','@"two words"','"two words"'),('quote','@"a\\\"b"','"a\\\"b"'),('hex','@"\\x61"','"a"')):
        yield Case('escaped/'+label,check('const S=struct {'+identifier+':u8, }; ',f'@bitOffsetOf(S,{name})==0'))
    for ty,bits in (('u0',0),('u9',9),('i129',129),('bool',1),('[2]u9',18),('[0:0]u9',9),('[2][3]u9',54),('[0]u9',0),('*S',64)):
        yield Case('logical-bits/'+ty,check('const S=struct { invalid:anyopaque, }; ',f'@bitSizeOf({ty})=={bits} and @TypeOf(@bitSizeOf({ty}))==comptime_int'))
    for label,prefix,ty in (
        ('empty','const S=struct {}; ','S'),
        ('ordinary','const S=struct { a:u8, b:u64, }; ','S'),
        ('zero-fields','const S=struct { a:void, b:u0, }; ','S'),
        ('invalid-field','const S=struct { a:anyopaque, }; ','S'),
        ('cycle','const S=struct { a:S, }; ','S'),
        ('invalid-default','const S=struct { a:u8=256, }; ','S'),
        ('comptime-field','const S=struct { comptime a:u8=1, }; ','S'),
        ('oversize','const S=struct { a:[4294967295]u8,b:u8, }; ','S'),
        ('array','const S=struct { a:u8, }; ','[3]S'),
        ('zero-array','const S=struct { a:u8, }; ','[0]S'),
        ('nested-zero-array','const S=struct { a:u8, }; ','[0][2]S'),
        ('zero-array-invalid','const S=struct { a:anyopaque, }; ','[0]S'),
        ('zero-array-cycle','const S=struct { a:S, }; ','[0]S'),
        ('slice','const S=struct { a:u8, }; ','[]S'),
    ):
        yield Case('no-representation/'+label,query(prefix,'@bitSizeOf('+ty+')'),'error')
    for label,prefix,expression in (
        ('arity-zero','','@bitOffsetOf()'),
        ('arity-one',base,'@bitOffsetOf(S)'),
        ('arity-three',base,'@bitOffsetOf(S,"item",0)'),
        ('type-before-name','','@bitOffsetOf(3,3)'),
        ('undefined-type-before-name','','@bitOffsetOf(@as(type,undefined),3)'),
        ('nonstruct-name-before-kind','','@bitOffsetOf(u8,3)'),
        ('cycle-name-before-layout','const S=struct { a:S, }; ','@bitOffsetOf(S,3)'),
        ('invalid-name-before-layout','const S=struct { a:anyopaque, }; ','@bitOffsetOf(S,undefined)'),
        ('cycle-before-missing','const S=struct { a:S, }; ','@bitOffsetOf(S,"missing")'),
        ('invalid-before-missing','const S=struct { a:anyopaque, }; ','@bitOffsetOf(S,"missing")'),
        ('nonstruct-kind','','@bitOffsetOf(u8,"item")'),
        ('zero-array-kind',base,'@bitOffsetOf([0]S,"item")'),
        ('zero-array-layout','const S=struct { a:anyopaque, }; ','@bitOffsetOf([0]S,"missing")'),
        ('zero-array-name-before-layout','const S=struct { a:anyopaque, }; ','@bitOffsetOf([0]S,3)'),
        ('size-limit','const S=struct { a:[4294967295]u8,b:u8, }; ','@bitOffsetOf(S,"missing")'),
        ('tail-align-limit','const S=struct { a:[4294967287]u8,b:u64, }; ','@bitOffsetOf(S,"b")'),
        ('bitsize-type-before-repr','','@bitSizeOf(3)'),
        ('bitsize-arity','','@bitSizeOf()'),
    ):
        yield Case('order/'+label,query(prefix,expression),'error')
    for label,name in (('missing','"missing"'),('declaration','"declaration"'),('method','"bad"'),('double-nul','"item\\x00\\x00"'),('nul-tail','"item\\x00tail"'),('prefix-nul','"\\x00item"')):
        yield Case('not-field/'+label,query(base,'@bitOffsetOf(S,'+name+')'),'error')
    yield Case('runtime-name',base+'fn f(name:[]const u8) usize { return @bitOffsetOf(S,name); } pub fn main() void { _=f("item"); }','error')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    records=[]
    failures=[]
    all_cases=list(cases())
    if not LAYOUTS or len(all_cases)>80:
        parser.error('missing oracle or bounded case cap exceeded')
    with tempfile.TemporaryDirectory(prefix='szc-struct-bit-layout-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(all_cases,1):
            path.write_text(case.source)
            if not args.reference_only:
                a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_bit_layout case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_bit_layout case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
            if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):
                failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):
                failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:
        args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct bit layout: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
