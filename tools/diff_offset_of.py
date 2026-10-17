#!/usr/bin/env python3
"""Compare bounded ordinary named-struct @offsetOf with official Zig.

Expected offsets are captured from official Zig 0.17.0 x86_64 ELF exports,
not modeled by a Python field sort. See docs/MILESTONES.md for provenance.
"""
import argparse
from dataclasses import dataclass
import itertools
import json
from pathlib import Path
import random
import tempfile

from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = 'emit-qbe'

@dataclass
class Case:
    label: str
    source: str
    lane: str = 'positive'


def layouts():
    result=[]
    for index, types in enumerate(itertools.permutations(('u8','u64','u16'))):
        result.append((f'perm3/{index}', [(f'f{i}',ty) for i,ty in enumerate(types)]))
    zero=[('a','u8'),('z','void'),('empty','[0]u64'),('bits','u0'),('b','u64')]
    for index in range(4):
        result.append((f'zero/{index}',zero[index:]+zero[:index]))
    wide=[('a','u513'),('b','i129'),('c','u24'),('d','u33'),('e','[3]u8'),('f','bool')]
    for index,fields in enumerate((wide,list(reversed(wide)),wide[2:]+wide[:2])):
        result.append((f'wide/{index}',fields))
    for count in (25,51,64):
        types=('u8','u64','u16','u65','void','[0]u32','u24','bool')
        fields=[(f'f{i}',types[i%len(types)]) for i in range(count)]
        shuffled=fields.copy()
        random.Random(0x0ff5e7+count).shuffle(shuffled)
        for label,order in (('forward',fields),('reverse',list(reversed(fields))),('shuffle',shuffled)):
            result.append((f'many/{count}/{label}',order))
    result.append(('nested',[('a','u8'),('inner','Inner'),('array','[3]Inner'),('matrix','[2][3]u16'),('z','void')]))
    result.append(('self-pointer',[('tag','u8'),('next','*const @This()'),('children','[]@This()')]))
    result.append(('all-zero',[('v','void'),('u','u0'),('a','[0]u64')]))
    result.append(('u32-size-limit',[('a','[4294967294]u8'),('b','u8')]))
    return result

PREFIX='const Inner=struct { byte:u8, word:u32, }; '
EXPECTED = {'inner': [8, 4, 4, 0],
 'name-base': [16, 8, 8, 0],
 'u32-size-limit': [4294967295, 1, 0, 4294967294],
 'all-zero': [0, 8, 0, 0, 0],
 'self-pointer': [32, 8, 24, 0, 8],
 'nested': [48, 4, 44, 0, 8, 32, 45],
 'many/64/shuffle': [256,
                     16,
                     228,
                     220,
                     232,
                     168,
                     255,
                     253,
                     252,
                     251,
                     251,
                     251,
                     230,
                     204,
                     136,
                     96,
                     204,
                     240,
                     208,
                     250,
                     152,
                     212,
                     128,
                     220,
                     249,
                     144,
                     245,
                     220,
                     236,
                     220,
                     176,
                     32,
                     112,
                     16,
                     224,
                     242,
                     242,
                     192,
                     220,
                     184,
                     234,
                     241,
                     220,
                     238,
                     216,
                     226,
                     243,
                     244,
                     48,
                     246,
                     246,
                     247,
                     248,
                     80,
                     249,
                     250,
                     212,
                     251,
                     200,
                     0,
                     196,
                     251,
                     160,
                     220,
                     254,
                     64],
 'many/64/reverse': [256,
                     16,
                     244,
                     220,
                     216,
                     255,
                     64,
                     234,
                     168,
                     254,
                     253,
                     212,
                     192,
                     253,
                     96,
                     230,
                     176,
                     252,
                     249,
                     192,
                     200,
                     249,
                     112,
                     228,
                     136,
                     248,
                     247,
                     200,
                     204,
                     245,
                     48,
                     232,
                     128,
                     240,
                     241,
                     208,
                     216,
                     241,
                     32,
                     236,
                     144,
                     242,
                     243,
                     216,
                     220,
                     242,
                     16,
                     226,
                     152,
                     245,
                     246,
                     204,
                     200,
                     248,
                     80,
                     224,
                     184,
                     250,
                     251,
                     196,
                     208,
                     253,
                     0,
                     238,
                     160,
                     255],
 'many/64/forward': [256,
                     16,
                     244,
                     136,
                     238,
                     16,
                     255,
                     216,
                     208,
                     254,
                     253,
                     168,
                     232,
                     80,
                     253,
                     208,
                     196,
                     252,
                     249,
                     184,
                     224,
                     112,
                     249,
                     204,
                     204,
                     248,
                     247,
                     176,
                     230,
                     64,
                     245,
                     208,
                     192,
                     240,
                     241,
                     152,
                     234,
                     32,
                     242,
                     216,
                     216,
                     242,
                     243,
                     144,
                     236,
                     0,
                     243,
                     204,
                     212,
                     245,
                     246,
                     160,
                     226,
                     96,
                     249,
                     208,
                     200,
                     250,
                     251,
                     128,
                     228,
                     48,
                     254,
                     208,
                     220,
                     255],
 'many/51/shuffle': [208,
                     16,
                     136,
                     178,
                     184,
                     188,
                     128,
                     186,
                     201,
                     193,
                     176,
                     202,
                     201,
                     104,
                     80,
                     156,
                     156,
                     0,
                     198,
                     164,
                     168,
                     198,
                     190,
                     192,
                     172,
                     176,
                     172,
                     182,
                     64,
                     144,
                     192,
                     195,
                     191,
                     176,
                     196,
                     190,
                     48,
                     32,
                     96,
                     193,
                     120,
                     194,
                     152,
                     168,
                     197,
                     168,
                     160,
                     199,
                     200,
                     112,
                     201,
                     180,
                     16],
 'many/51/reverse': [208,
                     16,
                     178,
                     96,
                     201,
                     199,
                     168,
                     160,
                     190,
                     48,
                     188,
                     128,
                     194,
                     198,
                     152,
                     152,
                     198,
                     80,
                     186,
                     136,
                     192,
                     191,
                     164,
                     168,
                     196,
                     0,
                     180,
                     144,
                     200,
                     195,
                     172,
                     152,
                     195,
                     64,
                     176,
                     120,
                     190,
                     193,
                     160,
                     160,
                     190,
                     32,
                     184,
                     112,
                     196,
                     197,
                     156,
                     156,
                     199,
                     16,
                     182,
                     104,
                     202],
 'many/51/forward': [208,
                     16,
                     198,
                     96,
                     182,
                     16,
                     199,
                     152,
                     164,
                     191,
                     202,
                     128,
                     176,
                     80,
                     200,
                     152,
                     156,
                     199,
                     194,
                     136,
                     186,
                     0,
                     190,
                     172,
                     172,
                     192,
                     193,
                     144,
                     178,
                     64,
                     202,
                     176,
                     168,
                     190,
                     197,
                     120,
                     188,
                     48,
                     190,
                     152,
                     160,
                     195,
                     196,
                     112,
                     184,
                     32,
                     199,
                     160,
                     152,
                     200,
                     201,
                     104,
                     180],
 'many/25/shuffle': [112,
                     16,
                     56,
                     96,
                     95,
                     76,
                     80,
                     84,
                     84,
                     84,
                     86,
                     0,
                     94,
                     94,
                     90,
                     94,
                     64,
                     32,
                     91,
                     92,
                     93,
                     48,
                     16,
                     88,
                     72,
                     96,
                     72],
 'many/25/reverse': [112,
                     16,
                     90,
                     95,
                     72,
                     76,
                     95,
                     32,
                     84,
                     56,
                     94,
                     91,
                     80,
                     84,
                     94,
                     0,
                     86,
                     64,
                     92,
                     93,
                     76,
                     80,
                     95,
                     16,
                     88,
                     48,
                     96],
 'many/25/forward': [112,
                     16,
                     90,
                     48,
                     88,
                     0,
                     95,
                     76,
                     76,
                     94,
                     93,
                     64,
                     86,
                     32,
                     94,
                     72,
                     80,
                     91,
                     92,
                     56,
                     84,
                     16,
                     95,
                     76,
                     72,
                     95,
                     96],
 'wide/2': [128, 16, 120, 112, 124, 127, 0, 80],
 'wide/1': [128, 16, 124, 125, 112, 120, 0, 32],
 'wide/0': [128, 16, 0, 80, 120, 112, 124, 127],
 'zero/3': [16, 8, 8, 0, 8, 9, 8],
 'zero/2': [16, 8, 0, 8, 0, 8, 9],
 'zero/1': [16, 8, 8, 0, 8, 0, 8],
 'zero/0': [16, 8, 8, 9, 0, 9, 0],
 'perm3/5': [16, 8, 8, 0, 10],
 'perm3/4': [16, 8, 8, 10, 0],
 'perm3/3': [16, 8, 0, 8, 10],
 'perm3/2': [16, 8, 0, 10, 8],
 'perm3/1': [16, 8, 10, 8, 0],
 'perm3/0': [16, 8, 10, 0, 8]}


def declaration(fields, name='S'):
    return 'const '+name+'=struct {'+','.join(f'{field}:{ty}' for field,ty in fields)+',}; '


def assertion(prefix, condition):
    return prefix+'pub fn main() void { const n:u8=if('+condition+')0 else 256; _=n; }'


def cases():
    for label,fields in layouts():
        numbers=EXPECTED[label]
        conditions=[f'@sizeOf(S)=={numbers[0]}',f'@alignOf(S)=={numbers[1]}']
        conditions += [f'@offsetOf(S,"{field}")=={offset}' for (field,_),offset in zip(fields,numbers[2:])]
        yield Case('layout/'+label,assertion(PREFIX+declaration(fields),' and '.join(conditions)))
    base='const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; '
    offset=EXPECTED['name-base'][2]
    for label,name in (
        ('plain','"item"'),('trailing-nul','"item\\x00"'),('concat','"i"++"tem"'),
        ('slice','".item."[1..5]'),('array','&[_]u8{105,116,101,109}'),
        ('inferred-array','&.{105,116,101,109}'),
    ):
        yield Case('name/'+label,assertion(base,f'@offsetOf(S,{name})=={offset}'))
    for label,identifier,query in (
        ('space','@"two words"','"two words"'),('keyword','@"fn"','"fn"'),
        ('quote','@"a\\\"b"','"a\\\"b"'),('hex','@"\\x61"','"a"'),
    ):
        yield Case('escaped/'+label,assertion('const S=struct { '+identifier+':u8, }; ',f'@offsetOf(S,{query})==0'))
    yield Case('result-comptime-int',assertion(base,'@TypeOf(@offsetOf(S,"item"))==comptime_int'))
    yield Case('type-context',base+'const A=[@offsetOf(S,"item")]u8; pub fn main() void { const n:u8=if(@sizeOf(A)=='+str(offset)+')0 else 256; _=n; }')
    yield Case('global-query',base+'const offset=@offsetOf(S,"item"); pub fn main() void { const n:u8=if(offset=='+str(offset)+')0 else 256; _=n; }')
    yield Case('nested-type',assertion('const S=struct { inner:Inner, }; '+PREFIX,'@offsetOf(@FieldType(S,"inner"),"word")=='+str(EXPECTED['inner'][3])))
    for name in ('"missing"','"declaration"','"bad"','"Item"','""','"item\\x00tail"','"item\\x00\\x00"','"\\x00item"'):
        yield Case('absent/'+name,base+'pub fn main() void { _=@offsetOf(S,'+name+'); }','error')
    for ty in ('u8','u0','bool','void','type','anyopaque','noreturn','f64','*S','[]u8','[3]u8','fn() void'):
        yield Case('nonstruct/'+ty,base+'pub fn main() void { _=@offsetOf('+ty+',"item"); }','error')
    for label,query in (
        ('zero-args','@offsetOf()'),('one-arg','@offsetOf(S)'),('three-args','@offsetOf(S,"item",0)'),
        ('non-type','@offsetOf(3,"item")'),('undefined-type','@offsetOf(@as(type,undefined),"item")'),
        ('integer-name','@offsetOf(S,3)'),('undefined-name','@offsetOf(S,undefined)'),
        ('wrong-slice-name','@offsetOf(S,@as([]const u16,undefined))'),
        ('array-name','@offsetOf(S,[_]u8{105,116,101,109})'),
        ('nonstruct-bad-name-order','@offsetOf(u8,3)'),
        ('undefined-type-bad-name-order','@offsetOf(@as(type,undefined),3)'),
    ):
        yield Case('arguments/'+label,base+'pub fn main() void { _='+query+'; }','error')
    yield Case('runtime-name',base+'fn f(name:[]const u8) usize { return @offsetOf(S,name); } pub fn main() void { _=f("item"); }','error')
    yield Case('empty-struct','const E=struct {}; pub fn main() void { _=@offsetOf(E,"x"); }','error')
    for label,prefix,query in (
        ('by-value-cycle','const S=struct { x:S, }; ','@offsetOf(S,"x")'),
        ('cycle-before-missing','const S=struct { x:S, }; ','@offsetOf(S,"missing")'),
        ('invalid-field-before-missing','const S=struct { x:anyopaque, }; ','@offsetOf(S,"missing")'),
        ('cycle-bad-name-order','const S=struct { x:S, }; ','@offsetOf(S,3)'),
        ('cycle-undefined-name-order','const S=struct { x:S, }; ','@offsetOf(S,undefined)'),
        ('invalid-field-bad-name-order','const S=struct { x:anyopaque, }; ','@offsetOf(S,3)'),
    ):
        yield Case('layout-error/'+label,prefix+'pub fn main() void { _='+query+'; }','error')


    for label,fields in (
        ('exact-overflow','a:[4294967295]u8,b:u8,'),
        ('tail-padding-overflow','a:[4294967287]u8,b:u64,'),
    ):
        prefix='const S=struct {'+fields+'}; '
        for query_label,query in (('size','@sizeOf(S)'),('present','@offsetOf(S,"b")'),('missing','@offsetOf(S,"missing")')):
            yield Case('size-limit/'+label+'/'+query_label,prefix+'pub fn main() void { _='+query+'; }','error')
    for label,prefix in (
        ('valid','const S=struct { x:u64, }; '),
        ('invalid-embedded','const S=struct { x:anyopaque, }; '),
        ('oversize','const S=struct { a:[4294967295]u8,b:u8, }; '),
    ):
        for query_label,name in (('valid-name','"x"'),('bad-name','3')):
            yield Case('zero-array-order/'+label+'/'+query_label,prefix+'pub fn main() void { _=@offsetOf([0]S,'+name+'); }','error')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    if not EXPECTED:
        parser.error('official offset oracle has not yet been captured')
    records=[]
    failures=[]
    with tempfile.TemporaryDirectory(prefix='szc-offset-of-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(cases(),1):
            path.write_text(case.source)
            if not args.reference_only:
                a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_offset_of case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_offset_of case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error))
            if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):
                failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):
                failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:
        args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'offsetOf: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
