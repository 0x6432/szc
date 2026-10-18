#!/usr/bin/env python3
"""Compare bounded immutable ordinary struct initialization/value projection.

No runtime struct storage, ABI, field addresses, defaults or other layouts.
"""
import argparse
from dataclasses import dataclass
import itertools
import json
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE='emit-qbe'

@dataclass
class Case:
    label:str
    source:str
    lane:str='positive'


def check(prefix,body,condition):
    return prefix+' pub fn main() void { '+body+' const n:u8=if('+condition+')0 else 256; _=n; }'


def use(prefix,body):
    return prefix+' pub fn main() void { '+body+' }'


def cases():
    for ty,value in (('u0','0'),('bool','true'),('u8','7'),('i8','-7'),('u17','65537'),('u64','123456789'),('u128','0x10000000000000000')):
        prefix=f'const S=struct {{ x:{ty}, }};'
        for form,init in (('typed',f'S{{.x={value}}}'),('context',f'.{{.x={value}}}'),('as',f'@as(S,.{{.x={value}}})')):
            if form=='as' and ty!='u8':
                continue
            body='const s'+(':S' if form=='context' else '')+'='+init+';'
            yield Case('scalar/'+ty+'/'+form,check(prefix,body,f's.x=={value} and @field(s,"x")=={value} and @TypeOf(s)==S and @TypeOf(s.x)=={ty}'))
    fields=(('a','u8','11'),('b','u64','22'),('c','u16','33'))
    for index,order in enumerate(itertools.permutations(fields)):
        prefix='const S=struct {'+','.join(name+':'+ty for name,ty,_ in order)+',};'
        init=','.join('.'+name+'='+val for name,_,val in reversed(order))
        for form in ('typed','context'):
            if form!=('typed' if index%2==0 else 'context'):
                continue
            body='const s'+(':S=.{'+init+'};' if form=='context' else '=S{'+init+'};')
            yield Case('order/'+str(index)+'/'+form,check(prefix,body,'s.a==11 and s.b==22 and s.c==33'))
    for label,prefix,body,condition in (
        ('copy','const S=struct { x:u8, };','const s=S{.x=7}; const copy:S=s;','copy.x==7 and @TypeOf(copy)==S'),
        ('global','const S=struct { x:u8, }; const global=S{.x=9};','','global.x==9 and @field(global,"x")==9'),
        ('temporary','const S=struct { x:u8, };','','(S{.x=13}).x==13 and @field(S{.x=14},"x")==14'),
        ('nested-typed','const I=struct { x:u8, }; const S=struct { inner:I, tag:u16, };','const s=S{.tag=3,.inner=I{.x=7}};','s.inner.x==7 and @field(@field(s,"inner"),"x")==7 and s.tag==3'),
        ('nested-context','const I=struct { x:u8, }; const S=struct { inner:I, };','const s:S=.{.inner=.{.x=8}};','s.inner.x==8 and @TypeOf(s.inner)==I'),
        ('array-field','const S=struct { a:[3]u8, };','const s=S{.a=.{3,5,7}};','s.a[0]==3 and s.a[2]==7 and @field(s,"a")[1]==5'),
        ('array-of-structs','const I=struct { x:u8, }; const S=struct { a:[2]I, };','const s:S=.{.a=.{.{.x=4},.{.x=9}}};','s.a[0].x==4 and s.a[1].x==9'),
        ('struct-array','const S=struct { x:u8, };','const a=[_]S{S{.x=2},S{.x=5}};','a[0].x==2 and a[1].x==5'),
        ('matrix-field','const S=struct { a:[2][2]u16, };','const s:S=.{.a=.{.{1,2},.{3,4}}};','s.a[1][0]==3 and s.a[0][1]==2'),
        ('pointer-field','const S=struct { p:*const u8, };','const s=S{.p=@ptrFromInt(4096)};','@intFromPtr(s.p)==4096 and @TypeOf(s.p)==*const u8'),
        ('many-pointer-field','const S=struct { p:[*]const u16, };','const s:S=.{.p=@ptrFromInt(4096)};','@intFromPtr(s.p+1)==4098'),
        ('slice-field','const S=struct { text:[]const u8, };','const s=S{.text="abc"};','s.text.len==3 and s.text[1]==98 and @field(s,"text").len==3'),
        ('sentinel-slice-field','const S=struct { text:[:0]const u8, };','const s:S=.{.text="abc"};','s.text.len==3 and s.text[3]==0'),
        ('synthetic-slice-field','const S=struct { data:[]const u16, };','const p:[*]const u16=@ptrFromInt(4096); const s=S{.data=p[0..3]};','s.data.len==3 and @intFromPtr(s.data.ptr)==4096'),
        ('empty-typed','const S=struct {};','const s=S{};','@TypeOf(s)==S and @sizeOf(@TypeOf(s))==0'),
        ('empty-context','const S=struct {};','const s:S=.{};','@TypeOf(s)==S'),
        ('empty-as','const S=struct {};','const s=@as(S,.{});','@TypeOf(s)==S'),
        ('zero-fields','const S=struct { v:void,u:u0,a:[0]u64, };','const s=S{.a=.{},.u=0,.v={}};','s.u==0 and @TypeOf(s.v)==void and s.a.len==0'),
        ('zero-array-fields','const S=struct { a:[3]u0, };','const s:S=.{.a=.{0,0,0}};','s.a[2]==0'),
        ('partial-undefined','const S=struct { a:u8,b:u16, };','const s=S{.a=undefined,.b=7};','s.b==7 and @TypeOf(s.a)==u8'),
        ('undefined-typed-field','const S=struct { x:u8, };','const s=S{.x=undefined}; const x:u8=s.x; _=x;','@TypeOf(s.x)==u8'),
        ('whole-undefined-type','const S=struct { x:u8, };','const s:S=undefined; const x=s.x; _=x;','@TypeOf(s.x)==u8'),
        ('whole-undefined-one-value','const S=struct { v:void,u:u0,a:[0]u64, };','const s:S=undefined;','s.u==0 and @TypeOf(s.v)==void and s.a.len==0'),
        ('undefined-inner-type','const I=struct { x:u8, }; const S=struct { inner:I, b:u8, };','const s:S=.{.inner=undefined,.b=9};','s.b==9 and @TypeOf(s.inner.x)==u8'),
        ('undefined-float-type','const S=struct { f:f32,g:f64, };','const s=S{.f=undefined,.g=undefined};','@TypeOf(s.f)==f32 and @TypeOf(s.g)==f64'),
        ('composed-name','const S=struct { item:u8, };','const s=S{.item=7};','@field(s,"i"++"tem")==7 and @field(s,".item."[1..5])==7'),
        ('nul-name','const S=struct { item:u8, };','const s=S{.item=7};','@field(s,"item\\x00")==7'),
    ):
        yield Case(label,check(prefix,body,condition))
    for label,identifier,query in (('space','@"two words"','"two words"'),('keyword','@"fn"','"fn"'),('quote','@"a\\\"b"','"a\\\"b"'),('hex','@"\\x61"','"a"')):
        prefix='const S=struct {'+identifier+':u8,};'
        yield Case('quoted/'+label,check(prefix,'const s=S{.'+identifier+'=7};','s.'+identifier+'==7 and @field(s,'+query+')==7'))
    for label,fields,init in (
        ('normal','a:u8,b:u16,','.a=1'),('empty-normal','a:u8,b:u16,',''),
        ('void','v:void,',''),('u0','u:u0,',''),('empty-array','a:[0]u64,',''),
        ('empty-struct','e:struct {},',''),('zero-array','a:[3]u0,',''),
    ):
        yield Case('missing/'+label,use('const S=struct {'+fields+'};','const s=S{'+init+'}; _=s;'),'error')
    for count in (25,64):
        fields=','.join(f'f{i}:'+('u8' if i%2==0 else 'u64') for i in range(count))+','
        init=','.join(f'.f{i}={i+1}' for i in reversed(range(count)))
        condition=' and '.join(f's.f{i}=={i+1}' for i in range(count))
        yield Case('many-fields/'+str(count),check('const S=struct {'+fields+'};','const s=S{'+init+'};',condition))
    yield Case('anonymous-nested-field',check('const S=struct { inner:struct { x:u8, }, };','const s:S=.{.inner=.{.x=7}};','s.inner.x==7'))
    yield Case('whole-undefined-builtin',check('const S=struct { x:u8, };','const s:S=undefined; const x=@field(s,"x"); _=x;','@TypeOf(@field(s,"x"))==u8'))
    base='const S=struct { a:u8,b:u8, };'
    for form in ('typed','context','as'):
        for label,body in (
            ('bad-before-unknown','.a=256,.unknown=1'),('unknown-before-bad','.unknown=1,.a=256'),
            ('unknown-bad-own-value','.unknown=256,.a=1,.b=2'),('bad-before-missing','.a=256'),
            ('duplicate-with-bad','.a=256,.a=1'),('duplicate-with-undeclared','.a=missing,.a=1'),
            ('unknown-undeclared','.unknown=missing,.a=1,.b=2'),
        ):
            init=('S{'+body+'}' if form=='typed' else '.{'+body+'}')
            if form=='as': init='@as(S,'+init+')'
            yield Case('validation/'+form+'/'+label,use(base,'const s'+(':S' if form=='context' else '')+'='+init+'; _=s;'),'error')
    for label,prefix,body in (
        ('duplicate-normalized-name','const S=struct { a:u8, };','const s=S{.a=1,.@"\\x61"=2}; _=s;'),
        ('unknown-member-value','const S=struct { a:u8, };','const s=S{.a=1}; _=s.missing;'),
        ('unknown-builtin-value','const S=struct { a:u8, };','const s=S{.a=1}; _=@field(s,"missing");'),
        ('namespace-not-value-field','const S=struct { a:u8,pub const declaration=7; };','const s=S{.a=1}; _=s.declaration;'),
        ('nominal-coercion','const A=struct { x:u8, }; const B=struct { x:u8, };','const a=A{.x=1}; const b:B=a; _=b;'),
        ('nominal-as','const A=struct { x:u8, }; const B=struct { x:u8, };','const a=A{.x=1}; const b=@as(B,a); _=b;'),
        ('nominal-nested','const A=struct { x:u8, }; const B=struct { x:u8, }; const S=struct { a:A, };','const s=S{.a=B{.x=1}}; _=s;'),
        ('wrong-scalar','const S=struct { a:u8, };','const s=S{.a=true}; _=s;'),
        ('undefined-whole-use','const S=struct { a:u8, };','const s:S=undefined; const n:u8=if(s.a==0)0 else 1; _=n;'),
        ('undefined-field-use','const S=struct { a:u8, };','const s=S{.a=undefined}; const n:u8=if(s.a==0)0 else 1; _=n;'),
        ('undefined-builtin-use','const S=struct { a:u8, };','const s:S=undefined; const n:u8=if(@field(s,"a")==0)0 else 1; _=n;'),
        ('embedded-nul-value-name','const S=struct { item:u8, };','const s=S{.item=1}; _=@field(s,"item\\x00tail");'),
        ('runtime-field-name','const S=struct { a:u8, }; const s=S{.a=1}; fn f(name:[]const u8) u8 { return @field(s,name); }','_=f("a");'),
    ):
        yield Case('error/'+label,use(prefix,body),'error')

    # Official context-lowering and bare-undefined contract regressions.
    yield Case('context-path/global-context','const S=struct { a:u8,b:u8, }; const s:S=.{.a=256,.unknown=1}; pub fn main() void { _=s; }','error')
    yield Case('context-path/nested-local-context','const S=struct { a:u8,b:u8, }; const O=struct { inner:S, }; pub fn main() void { const o:O=.{.inner=.{.a=256,.unknown=1}}; _=o; }','error')
    yield Case('context-path/nested-local-explicit','const S=struct { a:u8,b:u8, }; const O=struct { inner:S, }; pub fn main() void { const o=O{.inner=.{.a=256,.unknown=1}}; _=o; }','error')
    yield Case('context-path/nested-global-context','const S=struct { a:u8,b:u8, }; const O=struct { inner:S, }; const o:O=.{.inner=.{.a=256,.unknown=1}}; pub fn main() void { _=o; }','error')
    yield Case('context-path/array-explicit','const S=struct { a:u8,b:u8, }; pub fn main() void { const a=[_]S{.{.a=256,.unknown=1}}; _=a; }','error')
    yield Case('context-path/array-local-context','const S=struct { a:u8,b:u8, }; pub fn main() void { const a:[1]S=.{.{.a=256,.unknown=1}}; _=a; }','error')
    yield Case('context-path/array-global-context','const S=struct { a:u8,b:u8, }; const a:[1]S=.{.{.a=256,.unknown=1}}; pub fn main() void { _=a; }','error')
    yield Case('context-path/array-as-context','const S=struct { a:u8,b:u8, }; pub fn main() void { const a=@as([1]S,.{.{.a=256,.unknown=1}}); _=a; }','error')
    yield Case('context-path/whole-undefined-discard','const S=struct { a:u8,b:u8, }; pub fn main() void { const s:S=undefined; _=s; }')
    yield Case('context-path/whole-undefined-copy','const S=struct { a:u8,b:u8, }; pub fn main() void { const s:S=undefined; const copy=s; _=copy; }')
    yield Case('context-path/whole-undefined-missing','const S=struct { a:u8,b:u8, }; pub fn main() void { const s:S=undefined; _=s.missing; }','error')

    # Invalid returns must retain their own initializer diagnostics, not open ABI.
    yield Case('eager-value/typed/explicit-as','const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,256),.unknown=1}; _=s; }','error')
    yield Case('eager-value/typed/division','const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,1)/0,.unknown=1}; _=s; }','error')
    yield Case('eager-value/typed/arithmetic-overflow','const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,255)+1,.unknown=1}; _=s; }','error')
    yield Case('eager-value/as/explicit-as','const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=@as(S,.{.a=@as(u8,256),.unknown=1}); _=s; }','error')
    yield Case('eager-value/as/arithmetic-overflow','const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=@as(S,.{.a=@as(u8,255)+1,.unknown=1}); _=s; }','error')
    yield Case('logical-reference/known-pointer',check('const S=struct { a:u8,b:u8, };','const s=S{.a=1,.b=2}; const p=&s;','p.a==1 and p.b==2'))
    yield Case('logical-reference/known-builtin-pointer',check('const S=struct { a:u8,b:u8, };','const s=S{.a=1,.b=2}; const p=&s;','@field(p,"a")==1 and @field(p,"b")==2'))
    yield Case('logical-reference/undefined-pointer-target',check('const S=struct { a:u8,b:u8, };','const s:S=undefined; const p=&s; const x=p.a; _=x;','@TypeOf(p.a)==u8'))
    yield Case('logical-reference/known-struct-array-slice',check('const S=struct { a:u8,b:u8, };','const a=[_]S{S{.a=1,.b=2},S{.a=3,.b=4}}; const sl:[]const S=&a;','sl.len==2 and sl[0].a==1 and sl[1].b==4'))

    yield Case('undefined-one-possible-fields',check('const E=struct {}; const S=struct { u:u0,a:[3]u0,e:E, };','const s=S{.u=undefined,.a=undefined,.e=undefined};','s.u==0 and s.a[2]==0 and @TypeOf(s.e)==E'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    records=[]
    failures=[]
    with tempfile.TemporaryDirectory(prefix='szc-struct-values-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(cases(),1):
            path.write_text(case.source)
            if not args.reference_only:
                a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_values case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_values case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
            if args.reference_only and b.returncode!=(case.lane=='error'):
                failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):
                failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:
        args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct values: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:
        raise SystemExit('\n'.join(failures))

if __name__=='__main__':
    main()
