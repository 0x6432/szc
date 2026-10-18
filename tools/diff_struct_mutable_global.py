#!/usr/bin/env python3
"""Internal ordinary mutable struct/recursive-array globals: compile parity.
Source-known initializers only; no dynamic/comptime-call initialization, C ABI,
TLS/defaults/whole volatile copies or OPV struct global-read initialization.
"""
import argparse,json,tempfile
from dataclasses import dataclass
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
SZC_MODE='emit-qbe'
@dataclass
class Case:
 label:str
 source:str
 lane:str='positive'
BASE='const S=struct{a:u8,b:u16,}; '
G=BASE+'var g:S=.{.a=1,.b=2}; '
def program(prefix,body):return prefix+'pub fn main()void{'+body+'}'
def cases():
 for label,prefix,body in (
  ('literal',G,'g.a=7;_=g.b;'),
  ('inferred',BASE+'var g=S{.a=1,.b=2};','g.b=513;'),
  ('explicit-init',BASE+'var g:S=S{.a=1,.b=2};','_=g.a;'),
  ('as-init',BASE+'var g:S=@as(S,.{.a=1,.b=2});','_=g.a;'),
  ('const-source',BASE+'const initial:S=.{.a=7,.b=513};var g:S=initial;','g.a=2;'),
  ('undefined',BASE+'var g:S=undefined;','g.a=7;g.b=513;_=g.a;'),
  ('partial-undefined',BASE+'var g:S=.{.a=undefined,.b=513};','g.a=7;'),
  ('root-overalign',BASE+'var g:S align(64)=.{.a=1,.b=2};','g.a=7;_= &g;'),
  ('dot-alias',G+'const alias:*S=&g;','alias.a=7;_=g.a;'),
  ('builtin-alias',G,'@field(&g,"b")=513;'),
  ('whole-snapshot',G,'const old=g;g.a=7;_=old.b;'),
  ('contextual-overwrite',G,'g=.{.a=@intCast(g.b),.b=g.a};'),
  ('explicit-overwrite',G,'g=S{.a=@intCast(g.b),.b=g.a};'),
  ('as-overwrite',G,'g=@as(S,.{.a=@intCast(g.b),.b=g.a});'),
  ('named-distinct',G+'var h:S=.{.a=1,.b=2};','const n:u8=if(&g!=&h)0 else 256;_=n;'),
  ('field-qualifiers',G,'const p:*align(1)volatile allowzero S=&g;const q:*align(1)volatile allowzero u16=&p.b;q.*=513;'),
  ('volatile-scalar',G,'const p:*volatile S=&g;p.a=7;_=p.b;'),
  ('internal-value',G+'fn identity(s:S)S{return s;}','const old=identity(g);g.a=9;_=old.a;'),
  ('internal-alias',G+'fn f(s:S,p:*S)S{p.a=9;return s;}','g=f(g,&g);'),
  ('first-arg-before-mutate',G+'fn change()u8{g.a=9;return 1;}fn f(s:S,n:u8)u8{_=n;return s.a;}','_=f(g,change());'),
  ('defer-return',G+'fn f()S{defer g.a=9;return g;}','g=f();'),
  ('dynamic-root',G+'fn root()*S{return &g;}','root().a=7;_=root().b;'),
  ('nested',BASE+'const O=struct{i:S,tag:u8,};var g:O=.{.i=.{.a=1,.b=2},.tag=3};','const old=g.i;g.i.a=7;_=old.b;'),
  ('nested-volatile',BASE+'const O=struct{i:S,};var g:O=.{.i=.{.a=1,.b=2}};','const p:*volatile O=&g;p.i.b=513;_=p.i.a;'),
  ('array-of-struct',BASE+'var a:[2]S=.{.{.a=1,.b=2},.{.a=3,.b=4}};fn index()usize{return 1;}','a[index()].a=7;'),
  ('recursive-array',BASE+'var a:[2][1]S=.{.{.{.a=1,.b=2}},.{.{.a=3,.b=4}}};','const old=a;a[1][0].b=513;_=old[0][0].a;'),
  ('array-abi',BASE+'var a:[1]S=.{.{.a=1,.b=2}};fn identity(x:[1]S)[1]S{return x;}','a=identity(a);'),
  ('nested-array',BASE+'const O=struct{rows:[2]S,};var g:O=.{.rows=.{.{.a=1,.b=2},.{.a=3,.b=4}}};','g.rows[1].b=513;'),
  ('slice-view-of-global-array',BASE+'var a:[2]S=.{.{.a=1,.b=2},.{.a=3,.b=4}};','const s:[]S=&a;s[1].a=7;'),
  ('sentinel-field','const T=struct{bytes:[2:9]u8,};var g:T=.{.bytes=.{1,2}};','g.bytes[1]=7;_=g.bytes[2];'),
  ('zero-sentinel-field','const T=struct{bytes:[0:9]u8,x:u8,};var g:T=.{.bytes=.{},.x=7};','g.x=8;_=g.bytes[0];'),
  ('empty-struct','const E=struct{};var g:E=.{};','g=E{};_= &g;'),
  ('zero-array',BASE+'var a:[0]S=.{};','_= &a;'),
  ('zero-nested-array',BASE+'var a:[2][0]S=.{.{},.{}};','const old=a;_=old.len;'),
  ('zero-align-field','const T=struct{z:[0]u64,v:void,n:u0,x:u8,};var g:T=.{.z=.{},.v={},.n=0,.x=7};','g.x=8;g.n=0;'),
  ('pointer-field','const T=struct{p:*u8,};var byte:u8=7;var g:T=.{.p=&byte};','g.p.*=8;'),
  ('slice-field','const T=struct{s:[]u8,};var bytes=[_]u8{1,2,3};var g:T=.{.s=&bytes};','const old=g;g.s=g.s[0..1];old.s[1]=7;'),
  ('slice-volatile-descriptor','const T=struct{s:[]u8,};var bytes=[_]u8{1,2,3};var g:T=.{.s=&bytes};','const p:*volatile T=&g;const n=p.s.len;_=n;'),
  ('forward-address',BASE+'const H=struct{p:*S,};var h:H=.{.p=&later};var later:S=.{.a=7,.b=2};','h.p.a=8;'),
  ('self-address','const N=struct{p:*N,x:u8,};var g:N=.{.p=&g,.x=7};','g.p.x=8;'),
  ('mutual-address','const N=struct{p:*N,x:u8,};var a:N=.{.p=&b,.x=7};var b:N=.{.p=&a,.x=8};','a.p.p.x=9;'),
  ('callback',BASE+'fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&f};','_=h.cb(.{.a=7,.b=2});'),
  ('callback-array',BASE+'fn f(s:S)S{return s;}const H=struct{a:[1]*const fn(S)S,};var h:H=.{.a=.{&f}};','_=h.a[0](.{.a=7,.b=2});'),
  ('unused-bad-init',BASE+'var g:S=.{.a=256,.b=2};',''),
  ('unused-bad-callback',BASE+'fn bad(s:S)S{_=s;return .{.a="bad",.b=2};}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&bad};',''),
 ):
  yield Case(label,program(prefix,body))
 for label,prefix,body in (
  ('overflow',BASE+'var g:S=.{.a=256,.b=2};','_=g.a;'),
  ('typeof-live-init',BASE+'var g:S=.{.a=256,.b=2};','_=@TypeOf(&g);'),
  ('missing',BASE+'var g:S=.{.a=1};','_=g.a;'),
  ('unknown',BASE+'var g:S=.{.a=1,.b=2,.extra=3};','_=g.a;'),
  ('duplicate',BASE+'var g:S=.{.a=1,.a=2,.b=3};','_=g.a;'),
  ('mutable-read-init',G+'var h:S=g;','_=h.a;'),
  ('const-read-init',G+'const h:S=g;','_=h.a;'),
  ('const-alias-write',G,'const p:*const S=&g;p.a=7;'),
  ('bad-field',G,'g.missing=7;'),
  ('nominal-global-assign',G+'const T=struct{a:u8,b:u16,};const t:T=.{.a=1,.b=2};','g=t;'),
  ('array-len-live-init',BASE+'var a:[1]S=.{.{.a=256,.b=2}};','_=a.len;'),
  ('array-oob',BASE+'var a:[1]S=.{.{.a=1,.b=2}};','a[1].a=7;'),
  ('discard-volatile',G,'const p:*volatile S=&g;const q:*u8=&p.a;_=q;'),
  ('promote-alignment',G,'const p:*align(1)S=&g;const q:*u16=&p.b;_=q;'),
  ('callback-live-bad-body',BASE+'fn bad(s:S)S{_=s;return .{.a="bad",.b=2};}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&bad};extern fn consume(p:*H)void;','consume(&h);'),
 ):
  yield Case('error/'+label,program(prefix,body),'error')
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('szc');parser.add_argument('zig')
 parser.add_argument('--reference-only',action='store_true');parser.add_argument('--report',type=Path)
 args=parser.parse_args();records=[];failures=[];all_cases=list(cases())
 if len(all_cases)>65:parser.error('65-case bound exceeded')
 with tempfile.TemporaryDirectory(prefix='szc-struct-mutable-global-') as directory:
  path=Path(directory)/'test.zig'
  for index,case in enumerate(all_cases,1):
   path.write_text(case.source)
   if not args.reference_only:a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_mutable_global case {index} szc',timeout=120)
   b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_mutable_global case {index} zig',timeout=120)
   error=primary_error(b.stderr)
   records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
   if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):failures.append(f'reference assumption failed: {case.label}: {error}')
   if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
 if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
 print(f'struct mutable global: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
 if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
