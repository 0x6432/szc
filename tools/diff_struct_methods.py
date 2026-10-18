#!/usr/bin/env python3
"""Bounded ordinary method compile parity; no generic/comptime or aggregate C ABI."""
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
def program(prefix,body):return prefix+'pub fn main()void{'+body+'}\n'
S='const S=struct{x:u8,fn value(that:S)u8{return that.x;}fn read(that:*const S)u8{return that.x;}fn bump(that:*S,n:u8)u8{that.x+=n;return that.x;}};'
V='const s:S=.{.x=7};'
M='var s:S=.{.x=7};'
def cases():
 for label,prefix,body in (
  ('value-const',S,V+'_=s.value();'),
  ('value-mutable',S,M+'_=s.value();_= &s;'),
  ('const-pointer-const',S,V+'_=s.read();'),
  ('const-pointer-mutable',S,M+'_=s.read();_= &s;'),
  ('mutable-pointer',S,M+'_=s.bump(1);'),
  ('pointer-value',S,M+'const p:*S=&s;_=p.value();'),
  ('pointer-const',S,V+'const p:*const S=&s;_=p.read();'),
  ('pointer-mutate',S,M+'const p:*S=&s;_=p.bump(1);'),
  ('temporary-value',S,'_=(S{.x=7}).value();'),
  ('temporary-const-pointer',S,'_=(S{.x=7}).read();'),
  ('runtime-value',S+'fn f(x:u8)u8{const s:S=.{.x=x};return s.value();}','_=f(7);'),
  ('runtime-pointer',S+'fn f(p:*S)u8{return p.bump(1);}',M+'_=f(&s);'),
  ('runtime-returned-value',S+'fn make(n:u8)S{return .{.x=n};}','_=make(7).read();'),
  ('explicit-deref',S,M+'const p:*S=&s;_=(p.*).bump(1);'),
  ('grouped-object',S,V+'_=((s)).value();'),
  ('explicit-type',S,V+'_=S.value(s);_=S.read(&s);'),
  ('type-alias',S+'const T=S;',V+'_=T.value(s);'),
  ('stored-type-function',S,V+'const f=S.value;_=f(s);'),
  ('stored-type-function-pointer',S+'fn call(p:*const fn(S)u8,s:S)u8{return p(s);}',V+'_=call(&S.value,s);'),
  ('builtin-type-function',S,V+'_=@field(S,"value")(s);'),
  ('grouped-type-function',S,V+'_=(S.value)(s);'),
  ('static-namespace-zero','const S=struct{x:u8,fn zero()u8{return 7;}};','_=S.zero();'),
  ('namespace-function-alias','const S=struct{x:u8,fn get(s:S)u8{return s.x;}const alias=get;};',V+'_=s.alias();'),
  ('function-field','fn plus(n:u8)u8{return n+1;}const H=struct{cb:*const fn(u8)u8,};','const h:H=.{.cb=&plus};_=h.cb(7);'),
  ('builtin-function-field','fn plus(n:u8)u8{return n+1;}const H=struct{cb:*const fn(u8)u8,};','const h:H=.{.cb=&plus};_=@field(h,"cb")(7);'),
  ('grouped-function-field','fn plus(n:u8)u8{return n+1;}const H=struct{cb:*const fn(u8)u8,};','const h:H=.{.cb=&plus};_=(h.cb)(7);'),
  ('function-field-explicit-self',S+'const H=struct{cb:*const fn(S)u8,};',V+'const h:H=.{.cb=&S.value};_=h.cb(s);'),
  ('callback-default','fn plus(n:u8)u8{return n+1;}const H=struct{cb:*const fn(u8)u8=&plus,};','const h:H=.{};_=h.cb(7);'),
  ('nested-receiver',S+'const O=struct{i:S,};','var o:O=.{.i=.{.x=7}};_=o.i.bump(1);'),
  ('array-receiver',S+'fn index()usize{return 1;}','var a:[2]S=.{.{.x=1},.{.x=7}};_=a[index()].bump(1);'),
  ('global-receiver',S+'var g:S=.{.x=7};','_=g.bump(1);'),
  ('slice-receiver',S,'var a:[2]S=.{.{.x=1},.{.x=7}};const slice:[]S=&a;_=slice[1].bump(1);'),
  ('normalized-name','const S=struct{x:u8,fn read(s:S)u8{return s.x;}};',V+'_=s.@"re\\x61d"();'),
  ('quoted-name','const S=struct{x:u8,fn @"read value"(s:S)u8{return s.x;}};',V+'_=s.@"read value"();'),
  ('this-type','const S=struct{x:u8,const Self=@This();fn read(s:Self)u8{return s.x;}};',V+'_=s.read();'),
  ('defaults-and-return','const S=struct{x:u8=7,fn clone(s:S)S{return s;}};','const s:S=.{};_=s.clone().clone();'),
  ('contextual-aggregate-argument','const S=struct{x:u8=7,fn choose(s:S,t:S)S{_=s;return t;}};','const s:S=.{};_=s.choose(.{});'),
  ('pointer-chain','const S=struct{x:u8,fn same(s:*S)*S{return s;}fn read(s:*const S)u8{return s.x;}};',M+'_=s.same().read();'),
  ('underaligned-pointer','const S=struct{x:u64,fn read(s:*align(1)const S)u64{return s.x;}};','const p:*align(1)const S=@ptrFromInt(4097);_=p.read();'),
  ('volatile-scalar-method','const S=struct{x:u8,fn bump(s:*volatile S)u8{s.x+=1;return s.x;}};',M+'const p:*volatile S=&s;_=p.bump();'),
  ('implicit-C-export-pointer','const S=struct{x:u8,export fn read(s:*const S)u8{return s.x;}};',V+'_=s.read();'),
  ('implicit-C-extern-pointer','const S=struct{x:u8,extern fn read(s:*const S)u8;};',V+'_=s.read();'),
  ('public-private-reflection','const S=struct{x:u8,pub fn exposed(s:S)u8{return s.x;}fn hidden(s:S)u8{return s.x;}};',V+'_=s.exposed();_=s.hidden();const n:u8=if(@hasDecl(S,"exposed") and !@hasDecl(S,"hidden") and !@hasField(S,"exposed"))0 else 256;_=n;'),
  ('unused-bad-body','const S=struct{x:u8,pub fn bad(s:S)u8{_=s;return 256;}};','_=@sizeOf(S);_=@hasDecl(S,"bad");'),
  ('unused-bad-signature','const S=struct{x:u8,fn bad(s:S)@TypeOf(@as(u8,256)){_=s;return undefined;}};','_=@sizeOf(S);'),
 ):
  yield Case(label,program(prefix,body))
 for label,prefix,body in (
  ('const-to-mut',S,V+'_=s.bump(1);'),
  ('temporary-to-mut',S,'_=(S{.x=7}).bump(1);'),
  ('pointer-const-to-mut',S,V+'const p:*const S=&s;_=p.bump(1);'),
  ('stored-instance',S,V+'const f=s.value;_=f();'),
  ('grouped-instance-callee',S,V+'_=(s.value)();'),
  ('builtin-instance-callee',S,V+'_=@field(s,"value")();'),
  ('address-instance-function',S,V+'const p=&s.value;_=p;'),
  ('missing-member',S,V+'_=s.missing();'),
  ('wrong-first-scalar','const S=struct{x:u8,fn m(n:u8)u8{return n;}};',V+'_=s.m();'),
  ('wrong-first-nominal','const T=struct{x:u8,};const S=struct{x:u8,fn m(t:T)u8{return t.x;}};',V+'_=s.m();'),
  ('wrong-first-pointer','const T=struct{x:u8,};const S=struct{x:u8,fn m(t:*const T)u8{return t.x;}};',V+'_=s.m();'),
  ('no-first-parameter','const S=struct{x:u8,fn m()u8{return 7;}};',V+'_=s.m();'),
  ('too-few',S,M+'_=s.bump();'),
  ('too-many',S,V+'_=s.value(1);'),
  ('wrong-explicit-argument',S,M+'_=s.bump("bad");'),
  ('overflow-explicit-argument',S,M+'_=s.bump(256);'),
  ('namespace-count',S,'_=S.value();'),
  ('namespace-no-autoaddress',S,V+'_=S.read(s);'),
  ('namespace-wrong-nominal',S+'const T=struct{x:u8,};','const t:T=.{.x=7};_=S.value(t);'),
  ('double-pointer',S,M+'var p:*S=&s;const pp:**S=&p;_=pp.value();'),
  ('raw-many-deref',S,'const p:[*]S=@ptrFromInt(4096);_=(p.*).value();'),
  ('raw-slice-deref',S,'var a:[1]S=.{.{.x=7}};const slice:[]S=&a;_=(slice.*).value();'),
  ('scalar-field-called','const S=struct{x:u8,};',V+'_=s.x();'),
  ('callback-count','fn plus(n:u8)u8{return n+1;}const H=struct{cb:*const fn(u8)u8,};','const h:H=.{.cb=&plus};_=h.cb();'),
  ('namespace-function-pointer-not-method','const S=struct{x:u8,fn get(s:S)u8{return s.x;}const alias:*const fn(S)u8=&get;};',V+'_=s.alias();'),
  ('field-declaration-collision','const S=struct{x:u8,fn x(s:S)u8{return s.x;}};',V+'_=s.x();'),
  ('alignment-discard','const S=struct{x:u64,fn read(s:*const S)u64{return s.x;}};','const p:*align(1)const S=@ptrFromInt(4097);_=p.read();'),
  ('volatile-discard','const S=struct{x:u8,fn read(s:*const S)u8{return s.x;}};',M+'const p:*volatile S=&s;_=p.read();'),
  ('allowzero-discard','const S=struct{x:u8,fn read(s:*const S)u8{return s.x;}};','const p:*allowzero const S=@ptrFromInt(4096);_=p.read();'),
  ('called-bad-body','const S=struct{x:u8,fn bad(s:S)u8{_=s;return 256;}};',V+'_=s.bad();'),
  ('addressed-bad-body','const S=struct{x:u8,fn bad(s:S)u8{_=s;return 256;}};fn use(p:*const fn(S)u8)void{_=p;}','use(&S.bad);'),
  ('called-semantic-bad-signature','const S=struct{x:u8,fn bad(s:S)@TypeOf(@as(u8,256)){_=s;return undefined;}};',V+'_=s.bad();'),
  ('called-bad-signature','const S=struct{x:u8,fn bad(s:S)Missing{_=s;return undefined;}};',V+'_=s.bad();'),
 ):
  yield Case('error/'+label,program(prefix,body),'error')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');p.add_argument('zig');p.add_argument('--reference-only',action='store_true');p.add_argument('--report',type=Path);args=p.parse_args();records=[];failures=[];all_cases=list(cases())
 if not 60<=len(all_cases)<=80:p.error('60–80 case bound exceeded')
 with tempfile.TemporaryDirectory(prefix='szc-struct-methods-')as directory:
  path=Path(directory)/'test.zig'
  for index,case in enumerate(all_cases,1):
   path.write_text(case.source)
   if not args.reference_only:a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_methods case {index} szc',timeout=120)
   b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_methods case {index} zig',timeout=120)
   error=primary_error(b.stderr);records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
   if args.reference_only and (b.returncode!=(case.lane=='error')or(case.lane=='error'and error is None)):failures.append('reference assumption failed: '+case.label+'\n'+b.stderr.decode(errors='replace'))
   if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
   if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
 print(f'struct methods: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
 if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
