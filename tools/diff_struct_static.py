#!/usr/bin/env python3
"""Completed immutable ordinary struct backing and rooted typed field addresses.
No runtime struct values, mutable roots, ABI, reinterpret loads or address cycles.
"""
import argparse
from dataclasses import dataclass
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

BASE='const S=struct { a:u8, wide:u64, }; '
OBJ='const s:S=.{.a=7,.wide=19}; '

def cases():
    for label,body,condition in (
        ('dot','const p=&s.a;','p.*==7 and @TypeOf(p)==*const u8'),
        ('builtin','const p=&@field(s,"a");','p.*==7 and p==&s.a'),
        ('pointer-base','const p=&s; const q=&p.a;','q.*==7 and q==&s.a'),
        ('pointer-base-builtin','const p=&s; const q=&@field(p,"wide");','q.*==19 and q==&s.wide'),
        ('qualified','const p:*const volatile allowzero S=&s;','@TypeOf(&p.a)==*const volatile allowzero u8 and @TypeOf(&p.wide)==*const volatile allowzero u64'),
        ('underaligned','const p:*align(1) const S=&s;','@TypeOf(&p.wide)==*align(1) const u64 and @TypeOf(&p.a)==*const u8'),
        ('underaligned-all-flags','const p:*align(1) const volatile allowzero S=&s;','@TypeOf(&p.wide)==*align(1) const volatile allowzero u64'),
        ('qualifier-cast','const p:*const volatile u8=&s.a; _=p;','@TypeOf(&s.a)==*const u8'),
        ('root-deref','const p=&s; const q=&p.*.a;','q.*==7 and q==&s.a'),
    ):
        yield Case(label,check(BASE+OBJ,body,condition))
    for label,identifier,name in (('space','@"two words"','"two words"'),('keyword','@"fn"','"fn"'),('hex','@"\\x61"','"a"')):
        prefix='const S=struct {'+identifier+':u8,}; const s=S{.'+identifier+'=7};'
        yield Case('name/'+label,check(prefix,'const p=&@field(s,'+name+');','p.*==7 and p==&s.'+identifier))
    for label,name in (('nul','"a\\x00"'),('concat','""++"a"'),('array','&[_]u8{97}')):
        yield Case('name/'+label,check(BASE+OBJ,'const p=&@field(s,'+name+');','p.*==7 and p==&s.a'))
    for label,prefix,body,condition in (
        ('nested','const I=struct { x:u16, }; const S=struct { inner:I, tag:u8, }; const s:S=.{.inner=.{.x=513},.tag=7};','const p=&s.inner.x;','p.*==513 and p==&@field(@field(s,"inner"),"x")'),
        ('field-array-field','const I=struct { x:u16, }; const S=struct { a:[2]I, }; const s:S=.{.a=.{.{.x=11},.{.x=22}}};','const p=&s.a[1].x;','p.*==22 and p==&@field(s.a[1],"x")'),
        ('array-field','const S=struct { a:u8, }; const a=[_]S{.{.a=2},.{.a=9}};','const p=&a[1].a;','p.*==9 and p!=&a[0].a'),
        ('matrix-path','const S=struct { a:[2][2]u16, }; const s:S=.{.a=.{.{1,2},.{3,4}}};','const p=&s.a[1][0];','p.*==3'),
        ('sentinel-field','const S=struct { a:[2:9]u8, }; const s:S=.{.a=.{3,5}};','const p=&s.a[2];','p.*==9 and @TypeOf(&s.a)==*const [2:9]u8'),
        ('zero-sentinel-field','const S=struct { a:[0:9]u8, }; const s:S=.{.a=.{}};','const p=&s.a[0];','p.*==9'),
        ('slice-field','const S=struct { text:[]const u8, }; const s:S=.{.text="abc"};','const p=&s.text.len; const q=&s.text.ptr;','p.*==3 and q.*[1]==98'),
        ('pointer-field','const S=struct { p:*const u8, }; const byte:u8=31; const s:S=.{.p=&byte};','const p=&s.p;','p.*.*==31'),
        ('pointer-array-field','const S=struct { p:[2]*const u8, }; const a:u8=11; const b:u8=22; const s:S=.{.p=.{&a,&b}};','const p=&s.p[1];','p.*.*==22'),
        ('undefined-target-type',BASE+'const s:S=undefined;','const p=&s.a; const v=p.*; _=v;','@TypeOf(p)==*const u8 and @TypeOf(p.*)==u8'),
        ('partial-undefined',BASE+'const s:S=.{.a=undefined,.wide=19};','const p=&s.a; _=p;','(&s.wide).*==19'),
        ('empty-field','const E=struct {}; const S=struct { e:E, a:u8, }; const s:S=.{.e=.{},.a=7};','const p=&s.e;','@TypeOf(p)==*const E and @TypeOf(p.*)==E'),
        ('zero-overlap','const S=struct {a:void,b:u0,c:[0]u8,x:u8,}; const s:S=.{.a={},.b=0,.c=.{},.x=7};','const a:*const anyopaque=@ptrCast(&s.a); const b:*const anyopaque=@ptrCast(&s.b); const c:*const anyopaque=@ptrCast(&s.c);','@TypeOf(a)==*const anyopaque and @TypeOf(b)==*const anyopaque and @TypeOf(c)==*const anyopaque and (&s.b).*==0 and (&s.c).*.len==0'),
        ('global-distinct',BASE+'const a:S=.{.a=7,.wide=19}; const b:S=.{.a=7,.wide=19};','','&a==&a and &a!=&b and &a.a!=&b.a'),
        ('local-dedup',BASE,'const a:S=.{.a=7,.wide=19}; const b:S=.{.a=7,.wide=19};','&a==&b and &a.a==&b.a'),
        ('temporary-dedup',BASE,'const a=&S{.a=7,.wide=19}; const b=&S{.a=7,.wide=19};','a==b and &a.a==&b.a'),
        ('global-local-distinct',BASE+OBJ,'const local:S=.{.a=7,.wide=19};','&s!=&local and &s.a!=&local.a'),
        ('root-overalignment',BASE+'const s:S align(128)=.{.a=7,.wide=19};','','@TypeOf(&s)==*align(128) const S and @TypeOf(&s.wide)==*const u64 and @TypeOf(&s.a)==*const u8'),
        ('completed-struct-dependency','const I=struct {x:u32,}; const S=struct {p:*const I,}; const s:S=.{.p=&inner}; const inner:I=.{.x=73};','const p=&s.p.x;','p.*==73 and p==&inner.x'),
        ('completed-forward','const S=struct {p:*const u8,}; const s:S=.{.p=&byte}; const byte:u8=31;','','(&s.p).*.*==31'),
    ):
        yield Case(label,check(prefix,body,condition))
    for label,prefix,body in (
        ('discard-const',BASE+OBJ,'const p:*u8=&s.a; _=p;'),
        ('discard-volatile',BASE+OBJ,'const p:*const volatile S=&s; const q:*const u8=&p.a; _=q;'),
        ('discard-allowzero',BASE+OBJ,'const p:*allowzero const S=&s; const q:*const u8=&p.a; _=q;'),
        ('promote-alignment',BASE+OBJ,'const p:*align(16) const u64=&s.wide; _=p;'),
        ('under-promotion',BASE+OBJ,'const p:*align(1) const S=&s; const q:*const u64=&p.wide; _=q;'),
        ('undefined-base',BASE,'const p:*const S=undefined; _=&p.a;'),
        ('undefined-base-type',BASE,'const p:*const S=undefined; _=@TypeOf(&p.a);'),
        ('undefined-read',BASE+'const s:S=undefined;','const p=&s.a; const n:u8=if(p.*==0)0 else 256; _=n;'),
        ('partial-undefined-read',BASE+'const s:S=.{.a=undefined,.wide=19};','const n:u8=if((&s.a).*==0)0 else 256; _=n;'),
        ('missing',BASE+OBJ,'_=&s.missing;'),
        ('declaration','const S=struct {a:u8,pub const decl:u8=7;}; const s:S=.{.a=7};','_=&s.decl;'),
        ('double-nul',BASE+OBJ,'_=&@field(s,"a\\x00\\x00");'),
        ('nul-tail',BASE+OBJ,'_=&@field(s,"a\\x00tail");'),
        ('bad-name',BASE+OBJ,'_=&@field(s,3);'),
        ('undefined-name',BASE+OBJ,'_=&@field(s,undefined);'),
        ('oob-array','const S=struct { a:[2]u8, }; const s:S=.{.a=.{1,2}};','_=&s.a[2];'),
    ):
        yield Case('error/'+label,use(prefix,body),'error')

    yield Case('arithmetic/same-field-difference', check(
        'const S=struct {a:[3:9]u8,before:u64,}; const s:S=.{.a=.{1,2,3},.before=17};',
        'const p:[*]const u8=@ptrCast(&s.a); const q=p+2;',
        'q-p==2 and (q-2)[0]==1'))
    yield Case('error/field-relative-underflow', use(
        'const S=struct {a:[3:9]u8,before:u64,}; const s:S=.{.a=.{1,2,3},.before=17};',
        'const p:[*]const u8=@ptrCast(&s.a); const q=p-1; _=q;'), 'error')
    yield Case('error/negative-field-difference', use(
        'const S=struct {a:[3]u8,}; const s:S=.{.a=.{1,2,3}};',
        'const p:[*]const u8=@ptrCast(&s.a); const n=p-(p+1); _=n;'), 'error')

    yield from equality_cases()

def equality_cases():
    base='const S=struct {wide:u64,byte:u8,}; const s:S=.{.wide=9,.byte=7}; '
    for label,prefix,body,condition in (
        ('same-field-casts',base,'const p:*align(1) const volatile allowzero u64=&s.wide;', '@as(*allowzero const volatile anyopaque,@ptrCast(p))==@as(*allowzero const volatile anyopaque,@ptrCast(&s.wide))'),
        ('root-offset-provenance',base,'const bytes:[*]const u8=@ptrCast(&s); const field_bytes:[*]const u8=@ptrCast(&s.wide);', '@as(*const anyopaque,@ptrCast(&s))!=@as(*const anyopaque,@ptrCast(&s.wide)) and &bytes[@offsetOf(S,"byte")]!=&s.byte and &field_bytes[@offsetOf(S,"byte")]!=&s.byte'),
        ('array-offset-casts','const S=struct {a:[2]u16,}; const s:S=.{.a=.{11,22}};','const p:[*]const u16=@ptrCast(&s.a);','&p[0]==&s.a[0] and &p[1]==&s.a[1] and @as(*const anyopaque,@ptrCast(&s.a))==@as(*const anyopaque,@ptrCast(&s.a[0]))'),
        ('mixed-array-field','const I=struct{x:u8,}; const S=struct{a:[2]I,}; const s:S=.{.a=.{.{.x=1},.{.x=2}}};','const p:[*]const I=@ptrCast(&s.a);','&p[1].x==&s.a[1].x and &@field(p[1],"x")==&s.a[1].x'),
        ('zero-array-versus-field','const S=struct {a:[2]u0,b:void,}; const s:S=.{.a=.{0,0},.b={}};','','&s.a[0]==&s.a[1] and @as(*const anyopaque,@ptrCast(&s.a[0]))!=@as(*const anyopaque,@ptrCast(&s.b))'),
        ('overlapping-fields','const S=struct {a:void,b:u0,c:[0]u8,x:u8,}; const s:S=.{.a={},.b=0,.c=.{},.x=7};','const a:*const anyopaque=@ptrCast(&s.a); const b:*const anyopaque=@ptrCast(&s.b); const c:*const anyopaque=@ptrCast(&s.c);','a==a and a!=b and b!=c and @as(*const anyopaque,@ptrCast(&s))!=@as(*const anyopaque,@ptrCast(&s.x))'),
    ):
        yield Case('equality/'+label,check(prefix,body,condition))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc');parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true')
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    records=[];failures=[];all_cases=list(cases())
    if len(all_cases)>60:parser.error('60-case bound exceeded')
    with tempfile.TemporaryDirectory(prefix='szc-struct-static-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(all_cases,1):
            path.write_text(case.source)
            if not args.reference_only:
                a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_static case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_static case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
            if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):
                failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):
                failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct static: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
