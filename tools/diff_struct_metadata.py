#!/usr/bin/env python3
"""Ordinary named struct metadata differential; no runtime struct values.

Reference only: python3 diff_struct_metadata.py - /path/to/zig --reference-only
Unsupported features live in check_struct_metadata_unsupported.py (szc-only).
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


def check(prefix, condition):
    return prefix + f' pub fn main() void {{ const n:u8=if({condition})0 else 256; _=n; }}'


def cases():
    leaves = ('u0', 'void', 'bool', 'u8', 'i8', 'u9', 'i17', 'u24',
              'u33', 'i40', 'u65', 'u128', 'i129', 'u257', 'u513', 'f32', 'f64', '*u8',
              '*align(32) const volatile u8', '[*]const u8', '[]const u16',
              '[0]u64', '[3]u8', '[2][3]u16', '[3:0]u8')
    for ty in leaves:
        prefix = f'const S=struct {{ item:{ty}, }};'
        yield Case('leaf/'+ty, check(prefix, f'@hasField(S,"item") and !@hasField(S,"Item") and @FieldType(S,"item")=={ty} and @sizeOf(S)==@sizeOf({ty}) and @alignOf(S)==@alignOf({ty})'))
    layouts = (
        ('empty', '', 0, 1),
        ('zero', 'a:void, b:u0,', 0, 1),
        ('byte', 'a:u8, b:u0, c:void,', 1, 1),
        ('mixed-forward', 'a:u8, b:u64, c:u16,', 16, 8),
        ('mixed-reverse', 'c:u16, b:u64, a:u8,', 16, 8),
        ('odd', 'a:u9, b:i17, c:u33,', 16, 8),
        ('three-wide', 'a:u65, b:u8,', 32, 16),
        ('empty-array', 'a:[0]u64, b:u8,', 8, 8),
        ('zero-array', 'a:[8]u0, b:u8,', 1, 1),
        ('void-array', 'a:[4]void, b:u8,', 1, 1),
        ('pointer-alignment-not-pointee', 'a:*align(32) u8, b:u8,', 16, 8),
    )
    for label, fields, size, align in layouts:
        yield Case('layout/'+label, check('const S=struct {'+fields+'};', f'@sizeOf(S)=={size} and @alignOf(S)=={align}'))
    prefix = 'const I=struct { x:u8, y:u32, }; const S=struct { a:[3]I, b:[2][3]u16, z:void, };'
    yield Case('nested', check(prefix, '@FieldType(S,"a")==[3]I and @FieldType(I,"y")==u32 and @sizeOf(I)==8 and @alignOf(I)==4 and @sizeOf(S)==36 and @alignOf(S)==4'))
    prefix = 'const S=struct { next:*S, many:[*]const S, };'
    yield Case('self-pointers', check(prefix, '@FieldType(S,"next")==*S and @FieldType(S,"many")==[*]const S and @sizeOf(S)==16 and @alignOf(S)==8'))
    for source, target in (('*@This()', '*const @This()'),
                           ('*@This()', '*volatile @This()'),
                           ('[*]@This()', '[*]const @This()'),
                           ('[]@This()', '[]const @This()')):
        prefix = f'const S=struct {{ p:@TypeOf(@as({target},@as({source},undefined))), a:u8, }};'
        yield Case('recursive-qualifiers/'+target, check(prefix, '@hasField(S,"p") and @sizeOf(S)>0'))
    yield Case('lexical-self', check('const S=struct { next:*const @This(), };', '@FieldType(S,"next")==*const S and @sizeOf(S)==8'))
    yield Case('mutual-pointers', check('const A=struct { b:*B, }; const B=struct { a:*A, };', '@FieldType(A,"b")==*B and @FieldType(B,"a")==*A and @sizeOf(A)==8 and @sizeOf(B)==8'))
    prefix = 'const S=struct { field:u8, pub const declaration:u8=256; const Other=struct { x:u16, }; pub fn bad() u8 { return 256; } };'
    yield Case('declarations-not-fields', check(prefix, '@hasField(S,"field") and !@hasField(S,"declaration") and !@hasField(S,"bad") and !@hasField(S,"Other") and @FieldType(S,"field")==u8 and @FieldType(S.Other,"x")==u16 and @sizeOf(S)==1'))
    for label, identifier, query in (
        ('space', '@"two words"', '"two words"'),
        ('keyword', '@"fn"', '"fn"'),
        ('quote', '@"a\\\"b"', '"a\\\"b"'),
        ('escape-byte', '@"\\x61"', '"a"'),
        ('non-ascii', '@"caf\\xc3\\xa9"', '"caf\\xc3\\xa9"'),
    ):
        yield Case('name/'+label, check(f'const S=struct {{ {identifier}:u16, }};', f'@hasField(S,{query}) and @FieldType(S,{query})==u16'))
    for name, expected in (('"item"', True), ('"item\\x00"', True), ('"item\\x00tail"', False), ('"item\\x00\\x00"', False), ('"\\x00item"', False), ('""', False), ('"items"', False)):
        expr=f'@hasField(S,{name})'
        yield Case('nul-query/'+name, check('const S=struct { item:u8, };', expr if expected else '!'+expr))
    for name in ('"item\\x00"', '"i"++"tem"', '".item."[1..5]', '&[_]u8{105,116,101,109}'):
        yield Case('computed-field-name/'+name, check('const S=struct { item:u8, };', f'@FieldType(S,{name})==u8'))
    yield Case('nul-identifier', 'const S=struct { @"a\\x00b":u8, }; pub fn main() void { _=@sizeOf(S); }', 'error')
    yield Case('typed-query-result', check('const S=struct { x:u16, }; const T=@FieldType(S,"x"); const answer:T=19;', '@TypeOf(answer)==u16 and answer==19 and @TypeOf(@hasField(S,"x"))==bool and @TypeOf(@FieldType(S,"x"))==type'))
    yield Case('type-identity', check('const A=struct { x:u8, }; const B=struct { x:u8, };', 'A!=B and @FieldType(A,"x")==@FieldType(B,"x")'))
    yield Case('nested-anonymous', check('const S=struct { inner:struct { value:u16, }, };', '@FieldType(@FieldType(S,"inner"),"value")==u16 and @sizeOf(S)==2'))
    for fields in ('x:anyopaque,', 'x:i0,', 'x:3,', 'x:MissingType,', 'x:S,', 'x:u8, x:u16,'):
        for query in ('@hasField(S,"x")', '@sizeOf(S)'):
            yield Case('invalid-field/'+fields+'/'+query, 'const S=struct {'+fields+'}; pub fn main() void { _='+query+'; }', 'error')
    yield Case('nul-tail-fieldtype', 'const S=struct { item:u8, }; pub fn main() void { _=@FieldType(S,"item\\x00tail"); }', 'error')
    for query in ('@FieldType(S,"missing")', '@FieldType(S,"declaration")', '@FieldType(u8,"x")', '@FieldType(3,"x")', '@FieldType(S,3)', '@FieldType(S,undefined)', '@FieldType()', '@FieldType(S)', '@FieldType(S,"x",0)'):
        yield Case('invalid-query/'+query, 'const S=struct { x:u8, const declaration=1; }; pub fn main() void { _='+query+'; }', 'error')
    yield Case('runtime-name', 'const S=struct { x:u8, }; fn f(name:[]const u8) void { _=@FieldType(S,name); } pub fn main() void { f("x"); }', 'error')


    yield Case('forward-field-type', check('const S=struct { value:Byte, const Byte=u16; };', '@FieldType(S,"value")==u16 and @sizeOf(S)==2'))
    yield Case('self-slice', check('const S=struct { children:[]S, };', '@sizeOf(S)==16 and @alignOf(S)==8 and @FieldType(S,"children")==[]S'))
    yield Case('zero-array-alignment', check('const S=struct { z:[0]u64, };', '@sizeOf(S)==0 and @alignOf(S)==8 and @sizeOf([3]S)==0 and @alignOf([0]S)==8'))
    yield Case('unused-default-hasdecl', check('const S=struct { x:u8=256, pub const yes=true; };', '@hasDecl(S,"yes") and @sizeOf(*S)==8'))
    for count in (25, 64):
        fields = ','.join(f'f{i}:'+('u8' if i%2==0 else 'u64') for i in range(count))+','
        size = ((count//2*8 + (count+1)//2 + 7)//8)*8
        yield Case('many-fields/'+str(count), check('const S=struct {'+fields+'};', f'@sizeOf(S)=={size} and @alignOf(S)==8 and @hasField(S,"f0") and @hasField(S,"f{count-1}")'))
    for fields in ('x:u8, pub const x=1;', 'const x=1; x:u8,', '@"\\x78":u8, x:u16,'):
        yield Case('duplicate-field-member/'+fields, 'const S=struct {'+fields+'}; pub fn main() void { _=S; }', 'error')

    fields = ','.join(f'f{i}:struct {{ x:u8, }}' for i in range(150))+','
    yield Case('growing-type-table', check('const S=struct {'+fields+'};', '@sizeOf(S)==150 and @alignOf(S)==1 and @sizeOf(@FieldType(S,"f149"))==1'))

    yield Case('constant-struct-pointer-alignment', 'const S=struct {a:u8,b:u64,}; pub fn main() void { const p:*S=@ptrFromInt(4097); _=p; }', 'error')
    yield Case('nominal-type-mismatch', 'const A=struct {x:u8,}; const B=struct {x:u8,}; pub fn main() void { const p:*B=@as(*A,undefined); _=p; }', 'error')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    parser.add_argument('--reference-only', action='store_true')
    parser.add_argument('--report', type=Path)
    args=parser.parse_args()
    records=[]
    failures=[]
    with tempfile.TemporaryDirectory(prefix='szc-struct-metadata-') as directory:
        path=Path(directory)/'test.zig'
        for index, case in enumerate(cases(), 1):
            path.write_text(case.source)
            if not args.reference_only:
                a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_metadata case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_metadata case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            record=dict(index=index,label=case.label,lane=case.lane,zig_status=b.returncode,zig_error=error,source=case.source)
            records.append(record)
            expected_status=1 if case.lane=='error' else 0
            if args.reference_only and b.returncode!=expected_status:
                failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only:
                if a.returncode!=b.returncode or primary_error(a.stderr)!=error:
                    failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:
        args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct metadata: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
