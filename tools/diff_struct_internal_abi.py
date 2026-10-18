#!/usr/bin/env python3
"""Bounded internal ordinary-struct and recursive-array value ABI parity.
Compile acceptance/exact primary only; separate native fixtures establish timing/alias behavior.
C ABI, mutable globals, defaults and whole volatile copies remain separate NYI gates.
"""
import argparse,json,tempfile
from pathlib import Path
from dataclasses import dataclass
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
SZC_MODE='emit-qbe'
@dataclass
class Case:
    label:str
    source:str
    lane:str='positive'
BASE='const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} '
INIT='var s:S=.{.a=seed(1),.b=2}; '
def program(prefix,body):return prefix+'pub fn main()void{'+body+'}'
def cases():
    for label,defs,body in (
        ('parameter','fn f(s:S)u16{return s.b;}', '_=f(.{.a=seed(1),.b=2});'),
        ('return-literal','fn f(x:u8)S{return .{.a=x,.b=2};}', '_=f(seed(1));'),
        ('identity','fn f(s:S)S{return s;}', 'const s=f(.{.a=seed(1),.b=2});_=s.a;'),
        ('return-explicit','fn f(x:u8)S{return S{.a=x,.b=2};}', '_=f(seed(1));'),
        ('return-local','fn f(x:u8)S{var s:S=.{.a=x,.b=2};s.b+=1;return s;}', '_=f(seed(1));'),
        ('return-constant','const c:S=.{.a=1,.b=2};fn f()S{return c;}', '_=f();'),
        ('return-undefined','fn f()S{return undefined;}', '_=f();'),
        ('return-branch','fn f(x:u8)S{return if(x==1)S{.a=1,.b=2} else S{.a=3,.b=4};}', '_=f(seed(1));'),
        ('return-block','fn f(x:u8)S{return blk:{if(x==1)break :blk S{.a=1,.b=2};break :blk S{.a=3,.b=4};};}', '_=f(seed(1));'),
        ('private-copy','fn f(s:S)S{var t=s;t.a+=1;return t;}', INIT+'_=f(s);s.a=7;'),
        ('parameter-const-address','fn f(s:S)u8{const p:*const S=&s;return p.a;}', '_=f(.{.a=seed(1),.b=2});'),
        ('value-alias','fn f(s:S,p:*S)u16{p.b=9;return s.b;}', INIT+'_=f(s,&s);'),
        ('later-arg-effect','fn mutate(p:*S)u8{p.a=9;return 3;}fn f(s:S,x:u8)u8{_=x;return s.a;}', INIT+'_=f(s,mutate(&s));'),
        ('return-alias','fn f(s:S,p:*S)S{p.a=7;return s;}', INIT+'s=f(s,&s);'),
        ('defer-pointer-source','fn f(p:*S)S{defer p.a=9;return p.*;}', INIT+'_=f(&s);'),
        ('defer-local-source','fn f(x:u8)S{var s:S=.{.a=x,.b=2};defer s.a=9;return s;}', '_=f(seed(1));'),
        ('defer-parameter-copy','fn f(s:S)S{var t=s;defer t.a=9;return t;}', '_=f(.{.a=seed(1),.b=2});'),
        ('mixed-args','fn f(x:u8,s:S,y:u16,t:S,z:u8)S{return .{.a=x+s.a+z,.b=s.b+y+t.b};}', '_=f(seed(1),.{.a=2,.b=3},4,.{.a=5,.b=6},7);'),
        ('recursion','fn f(s:S,n:u8)S{if(n==0)return s;var t=s;t.a+=1;return f(t,n-1);}', '_=f(.{.a=seed(1),.b=2},3);'),
        ('two-results','fn f(x:u8)S{return .{.a=x,.b=2};}', 'const a=f(seed(1));const b=f(seed(2));_=a.a+b.a;'),
        ('under-aligned-source','fn f(s:S)u16{return s.b;}fn read(p:*align(1)const S)u16{return f(p.*);}', INIT+'_=read(&s);'),
        ('overaligned-local','fn f(s:S)S{var t:S align(64)=s;t.a+=1;return t;}', '_=f(.{.a=seed(1),.b=2});'),
        ('return-nested','const O=struct{inner:S,};fn f(o:O)S{return o.inner;}', '_=f(.{.inner=.{.a=seed(1),.b=2}});'),
    ):yield Case(label,program(BASE+defs,body))
    for label,defs,body in (
        ('array-parameter','fn f(a:[2]S)u16{return a[1].b;}', '_=f(.{.{.a=seed(1),.b=2},.{.a=3,.b=4}});'),
        ('array-return','fn f(x:u8)[2]S{return .{.{.a=x,.b=2},.{.a=3,.b=4}};}', '_=f(seed(1));'),
        ('array-identity','fn f(a:[2]S)[2]S{return a;}', '_=f(.{.{.a=seed(1),.b=2},.{.a=3,.b=4}});'),
        ('recursive-array','fn f(a:[2][1]S)[2][1]S{return a;}', '_=f(.{.{.{.a=seed(1),.b=2}},.{.{.a=3,.b=4}}});'),
        ('array-later-effect','fn m(p:*[1]S)u8{p[0].a=9;return 1;}fn f(a:[1]S,x:u8)u8{_=x;return a[0].a;}', 'var a:[1]S=.{.{.a=seed(1),.b=2}};_=f(a,m(&a));'),
        ('nested-struct-array','const O=struct{rows:[2]S,};fn f(o:O)O{return o;}', '_=f(.{.rows=.{.{.a=seed(1),.b=2},.{.a=3,.b=4}}});'),
        ('zero-array','fn f(a:[0]S)[0]S{return a;}', '_=f(.{});'),
        ('zero-outer-array','fn f(a:[0][2]S)[0][2]S{return a;}', '_=f(.{});'),
        ('zero-inner-array','fn f(a:[2][0]S)[2][0]S{return a;}', '_=f(.{.{},.{}});'),
        ('empty-struct','const E=struct{};fn f(e:E)E{return e;}', '_=f(.{});'),
        ('zero-aligned-field','const Z=struct{z:[0]u64,v:void,n:u0,};fn f(z:Z)Z{return z;}', '_=f(.{.z=.{},.v={},.n=0});'),
        ('sentinel-field','const T=struct{b:[2:9]u8,};fn f(t:T)T{return t;}', '_=f(.{.b=.{seed(1),2}});'),
        ('zero-sentinel-field','const T=struct{b:[0:9]u8,a:u8,};fn f(t:T)T{return t;}', '_=f(.{.b=.{},.a=seed(1)});'),
        ('slice-pointer-field','const T=struct{s:[]const u8,p:*const u8,};const byte:u8=7;fn f(t:T)T{return t;}', '_=f(.{.s="abc",.p=&byte});'),
    ):yield Case(label,program(BASE+defs,body))
    for label,defs,body in (
        ('indirect-known','fn f(s:S)S{return s;}', 'const p:*const fn(S)S=&f;_=p(.{.a=seed(1),.b=2});'),
        ('indirect-runtime','fn f(s:S)S{return s;}fn g(s:S)S{return s;}fn choose(x:u8)*const fn(S)S{return if(x==1)&f else &g;}', '_=choose(seed(1))(.{.a=seed(1),.b=2});'),
        ('indirect-array','fn f(a:[1]S)[1]S{return a;}', 'const p:*const fn([1]S)[1]S=&f;_=p(.{.{.a=seed(1),.b=2}});'),
        ('callback-struct','fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};', '_=h.cb(.{.a=seed(1),.b=2});'),
        ('callback-array','fn f(s:S)S{return s;}const a=[_]*const fn(S)S{&f};', '_=a[seed(0)](.{.a=seed(1),.b=2});'),
        ('callback-slice','fn f(s:S)S{return s;}const a=[_]*const fn(S)S{&f};const view:[]const *const fn(S)S=&a;', '_=view[seed(0)](.{.a=seed(1),.b=2});'),
        ('aggregate-function-address','fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};extern fn consume(p:*const H)void;', 'consume(&h);'),
        ('dead-bad-body','fn bad(s:S)S{_=s;return .{.a="bad",.b=2};}', '_=seed(1);'),
        ('unused-parameter','fn f(s:S)u8{_=s;return 7;}', '_=f(.{.a=seed(1),.b=2});'),
        ('indirect-mixed','fn f(x:u8,s:S,y:u16)S{return .{.a=s.a+x,.b=s.b+y};}', 'const p:*const fn(u8,S,u16)S=&f;_=p(seed(1),.{.a=2,.b=3},4);'),
    ):yield Case(label,program(BASE+defs,body))
    for label,defs,body in (
        ('nominal-param','const T=struct{a:u8,b:u16,};fn f(s:S)u8{return s.a;}', 'const t:T=.{.a=seed(1),.b=2};_=f(t);'),
        ('nominal-return','const T=struct{a:u8,b:u16,};fn f()S{const t:T=.{.a=1,.b=2};return t;}', '_=f();'),
        ('nominal-array','const T=struct{a:u8,b:u16,};fn f(a:[1]S)u8{return a[0].a;}', 'const t:[1]T=.{.{.a=seed(1),.b=2}};_=f(t);'),
        ('parameter-mutation','fn f(s:S)u8{s.a=7;return s.a;}', '_=f(.{.a=seed(1),.b=2});'),
        ('parameter-mutable-address','fn f(s:S)u8{const p:*S=&s;return p.a;}', '_=f(.{.a=seed(1),.b=2});'),
        ('return-pointer-const','fn f(s:S)*u8{return &s.a;}', '_=f(.{.a=seed(1),.b=2});'),
        ('few-args','fn f(s:S,x:u8)u8{return s.a+x;}', '_=f(.{.a=seed(1),.b=2});'),
        ('many-args','fn f(s:S)u8{return s.a;}', '_=f(.{.a=seed(1),.b=2},3);'),
        ('indirect-few','fn f(s:S,x:u8)u8{return s.a+x;}', 'const p:*const fn(S,u8)u8=&f;_=p(.{.a=seed(1),.b=2});'),
        ('indirect-many','fn f(s:S)u8{return s.a;}', 'const p:*const fn(S)u8=&f;_=p(.{.a=seed(1),.b=2},3);'),
        ('bad-arg-field','fn f(s:S)u8{return s.a;}', '_=f(.{.a=256,.b=seed(1)});'),
        ('missing-arg-field','fn f(s:S)u8{return s.a;}', '_=f(.{.a=seed(1)});'),
        ('bad-return-field','fn f(x:u8)S{return .{.a=x,.b="bad"};}', '_=f(seed(1));'),
        ('function-pointer-type','fn f(s:S)S{return s;}', 'const p:*const fn(S)u8=&f;_=p;'),
        ('live-callback-bad-body','fn f(s:S)S{_=s;return .{.a="bad",.b=2};}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};extern fn consume(p:*const H)void;', 'consume(&h);'),
    ):yield Case('error/'+label,program(BASE+defs,body),'error')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('szc');parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true');parser.add_argument('--report',type=Path)
    args=parser.parse_args();records=[];failures=[];all_cases=list(cases())
    if len(all_cases)>75:parser.error('75-case bound exceeded')
    with tempfile.TemporaryDirectory(prefix='szc-struct-internal-abi-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(all_cases,1):
            path.write_text(case.source)
            if not args.reference_only:a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_internal_abi case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_internal_abi case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
            if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct internal ABI: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
