#!/usr/bin/env python3
"""Ordinary typed field defaults: bounded compile parity, with preserved queue evidence.
Set PYTHONPATH to tools/. The six marked queued/synchronous cases always compare
all ordered errors; other cases use the established primary/status convention.
"""
import argparse,json,re,tempfile
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
 diagnostics:bool=False
def program(prefix,body):return prefix+'pub fn main()void{'+body+'}\n'
def check(condition):return 'const check:u8=if('+condition+')0 else 256;_=check;'
BASE='const S=struct{a:u8=7,b:u16=513,};'
I='const I=struct{x:u8=7,y:u16=513,};'
BAD='const S=struct{a:u8=256,b:u16,};'
def cases():
 for label,prefix,body in (
  ('all-explicit',BASE,'const s=S{.a=1,.b=2};'+check('s.a==1 and s.b==2')),
  ('all-defaults-explicit',BASE,'const s=S{};'+check('s.a==7 and s.b==513')),
  ('all-defaults-context',BASE,'const s:S=.{};'+check('s.a==7 and s.b==513')),
  ('all-defaults-as',BASE,'const s=@as(S,.{});'+check('s.a==7 and s.b==513')),
  ('first-override',BASE,'const s:S=.{.a=2};'+check('s.a==2 and s.b==513')),
  ('last-override',BASE,'const s:S=.{.b=3};'+check('s.a==7 and s.b==3')),
  ('required-and-default','const S=struct{a:u8,b:u16=513,};','const s:S=.{.a=7};'+check('s.b==513')),
  ('undefined-is-default','const S=struct{a:u8=undefined,b:u8=7,};','const s:S=.{};'+check('s.b==7')),
  ('whole-undefined-ignores-valid-default',BASE,'const s:S=undefined;_=@TypeOf(s.a);'),
  ('void-zero','const S=struct{a:u0=0,v:void={},};','const s:S=.{};'+check('s.a==0')),
  ('odd-width','const S=struct{x:u3=7,y:i9=-256,};','const s:S=.{};'+check('s.x==7 and s.y == -256')),
  ('wide-known','const S=struct{x:u128=18446744073709551616,};','const s:S=.{};'+check('s.x==18446744073709551616')),
  ('bool-if','const S=struct{x:bool=true,y:u8=if(true)7 else 9,};','const s:S=.{};'+check('s.x and s.y==7')),
  ('arithmetic','const n:u8=3;const S=struct{x:u8=n+4,};','const s:S=.{};'+check('s.x==7')),
  ('block','const S=struct{x:u8=blk:{const n:u8=3;break :blk n+4;},};','const s:S=.{};'+check('s.x==7')),
  ('intCast-context','const S=struct{x:u8=@intCast(@as(u16,7)),};','const s:S=.{};'+check('s.x==7')),
  ('truncate-context','const S=struct{x:u8=@truncate(@as(u16,263)),};','const s:S=.{};'+check('s.x==7')),
  ('nested-context',I+'const O=struct{i:I=.{},};','const o:O=.{};'+check('o.i.x==7 and o.i.y==513')),
  ('nested-overridden',I+'const O=struct{i:I=.{.x=9},};','const o:O=.{};'+check('o.i.x==9 and o.i.y==513')),
  ('nested-explicit',I+'const O=struct{i:I=I{},};','const o:O=.{};'+check('o.i.x==7')),
  ('array-default','const S=struct{a:[2]u8=.{1,2},};','const s:S=.{};'+check('s.a[0]==1 and s.a[1]==2')),
  ('sentinel-default','const S=struct{a:[2:9]u8=.{1,2},};','const s:S=.{};'+check('s.a[2]==9')),
  ('zero-array-default','const S=struct{a:[0]u64=.{},x:u8=7,};','const s:S=.{};'+check('s.a.len==0 and s.x==7')),
  ('zero-sentinel-default','const S=struct{a:[0:9]u8=.{},};','const s:S=.{};'+check('s.a[0]==9')),
  ('array-struct-default',I+'const O=struct{a:[2]I=.{.{},.{.x=4}},};','const o:O=.{};'+check('o.a[0].x==7 and o.a[1].x==4')),
  ('recursive-array-default',I+'const O=struct{a:[1][1]I=.{.{.{}}},};','const o:O=.{};'+check('o.a[0][0].y==513')),
  ('pointer-default','const byte:u8=7;const S=struct{p:*const u8=&byte,};','const s:S=.{};'+check('s.p.*==7')),
  ('slice-default','const bytes=[_]u8{1,2};const S=struct{s:[]const u8=&bytes,};','const s:S=.{};'+check('s.s.len==2 and s.s[1]==2')),
  ('mutable-pointer-default','var byte:u8=7;const S=struct{p:*u8=&byte,};','var s:S=.{};s.p.*=8;_= &s;'),
  ('nul-name-invalid','const S=struct{@"a b":u8=7,@"a\\x00b":u8=9,};','const s:S=.{};'+check('@field(s,"a b")==7 and @field(s,"a\\x00b")==9')),
  ('quoted-space-name','const S=struct{@"a b":u8=7,};','const s:S=.{};'+check('@field(s,"a b")==7')),
  ('unused-callback-default-lazy','fn bad(x:u8)u8{return @as(u8,256)+x;}const H=struct{p:*const fn(u8)u8=&bad,};',''),
  ('declaration-same-name','const x:u8=7;const S=struct{x:u8=x,};','const s:S=.{};'+check('s.x==7')),
  ('namespace-constant','const S=struct{x:u8=n,const n:u8=7;};','const s:S=.{};'+check('s.x==7')),
  ('local-capture','','const n:u8=7;const S=struct{x:u8=n,};const s:S=.{};'+check('s.x==7')),
  ('local-type-capture','','const T=u8;const n:T=7;const S=struct{x:T=n,};const s:S=.{};'+check('s.x==7')),
  ('self-explicit-default','const S=struct{x:u8=(S{.x=7}).x,};','const s:S=.{};'+check('s.x==7')),
  ('self-layout-query-default','const S=struct{x:u8=@intCast(@sizeOf(S)),};','const s:S=.{};'+check('s.x==1')),
  ('mutable-local',BASE,'var s:S=.{};s.a=9;_= &s;'),
  ('runtime-field-plus-default',BASE+'fn f(n:u8)S{return .{.a=n};}','_=f(3);'),
  ('runtime-assignment',BASE+'fn f(p:*S,n:u8)void{p.*=.{.a=n};}','var s:S=.{};f(&s,3);'),
  ('global-default',BASE+'var g:S=.{};','g.a=9;'),
  ('global-array-default',BASE+'var g:[2]S=.{.{},.{.a=3}};','g[1].b=7;'),
  ('known-address',BASE+'const g:S=.{};fn f(p:*const u16)u16{return p.*;}','_=f(&g.b);'),
  ('internal-abi',BASE+'fn f(s:S)S{return s;}','_=f(.{});'),
  ('indirect-abi',BASE+'fn f(s:S)S{return s;}fn call(p:*const fn(S)S,s:S)S{return p(s);}','_=call(&f,.{});'),
  ('callback-default',BASE+'fn f(s:S)S{return s;}const H=struct{p:*const fn(S)S=&f,};','const h:H=.{};_=h.p(.{});'),
  ('callback-default-body-live','fn bad(x:u8)u8{return @as(u8,256)+x;}const H=struct{p:*const fn(u8)u8=&bad,};','_=@sizeOf(H);'),
  ('unused-invalid-default',BAD,''),
  ('hasDecl-invalid-lazy',BAD,'_=@hasDecl(S,"a");'),
  ('pointer-size-invalid-lazy',BAD,'_=@sizeOf(*S);'),
  ('pointer-type-invalid-lazy',BAD,'const P=*S;_=@alignOf(P);'),
 ):
  yield Case(label,program(prefix,body),'error' if label in ('nul-name-invalid','callback-default-body-live') else 'positive')
 for label,prefix,body in (
  ('size-valid',BASE,'_=@sizeOf(S);'),
  ('offset-valid',BASE,'_=@offsetOf(S,"b");'),
  ('bitOffset-valid',BASE,'_=@bitOffsetOf(S,"b");'),
  ('size-invalid',BAD,'_=@sizeOf(S);'),
  ('align-invalid',BAD,'_=@alignOf(S);'),
  ('hasField-invalid',BAD,'_=@hasField(S,"a");'),
  ('FieldType-invalid',BAD,'_=@FieldType(S,"a");'),
  ('offset-invalid',BAD,'_=@offsetOf(S,"b");'),
  ('bitOffset-invalid',BAD,'_=@bitOffsetOf(S,"b");'),
  ('overridden-invalid',BAD,'const s=S{.a=1,.b=2};_=s;'),
  ('undefined-still-layout-invalid',BAD,'const s:S=undefined;_=&s;'),
  ('omitted-invalid',BAD,'const s:S=.{.b=2};_=s;'),
  ('missing-before-invalid','const S=struct{required:u16,a:u8=256,};','const s:S=.{};_=s;'),
  ('missing-valid-default','const S=struct{required:u16,a:u8=7,};','const s:S=.{};_=s;'),
  ('wrong-default-type','const S=struct{x:u8="bad",};','_=@sizeOf(S);'),
  ('negative-default','const S=struct{x:u8=-1,};','const s:S=.{};_=s;'),
  ('default-runtime-global','var n:u8=7;const S=struct{x:u8=n,};','const s:S=.{};_=s;'),
  ('nominal-default','const I=struct{x:u8,};const J=struct{x:u8,};const S=struct{i:I=J{.x=7},};','const s:S=.{};_=s;'),
  ('array-length-default','const S=struct{x:[2]u8=.{1},};','const s:S=.{};_=s;'),
  ('nested-missing-default','const I=struct{x:u8,};const S=struct{i:I=.{},};','const s:S=.{};_=s;'),
  ('unknown-over-valid',BASE,'const s=S{.other=1};_=s;'),
  ('duplicate-over-valid',BASE,'const s=S{.a=1,.a=2};_=s;'),
  ('explicit-overflow-valid-default',BASE,'const s:S=.{.a=256};_=s;'),
  ('default-pointer-const','const byte:u8=7;const S=struct{p:*u8=&byte,};','const s:S=.{};_=s;'),
 ):
  yield Case(label,program(prefix,body),'positive' if label.endswith('-valid') and label in ('size-valid','offset-valid','bitOffset-valid') else 'error')
 for label,source in (
  ('queue-then-main',program(BAD,'_=@sizeOf(S);const n:u16=65536;_=n;')),
  ('sync-suppresses-main',program(BAD,'const s:S=.{.b=2};_=s;const n:u16=65536;_=n;')),
  ('explicit-value-and-queue',program(BAD,'const s=S{.b=65536};_=s;')),
  ('context-value-and-queue',program(BAD,'const s:S=.{.b=65536};_=s;')),
  ('queue-definition-after-main',program('','_=@sizeOf(S);const n:u16=65536;_=n;')+BAD+'\n'),
  ('return-and-main-independent',program(BAD+'fn f()S{return .{.b=2};}','_=f();const n:u16=65536;_=n;')),
 ):
  yield Case('diagnostics/'+label,source,'error',True)
def error_list(stderr):
 # Includes unlocated errors; excludes notes and reference traces.
 return re.findall(r'(?:^|\n)(?:[^\n]*?:\d+:\d+: )?error: ([^\n]+)',stderr.decode(errors='replace'))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');p.add_argument('zig');p.add_argument('--reference-only',action='store_true');p.add_argument('--report',type=Path);args=p.parse_args()
 records=[];failures=[];all_cases=list(cases())
 if len(all_cases)>90:p.error('90-case bound exceeded')
 with tempfile.TemporaryDirectory(prefix='szc-struct-defaults-') as directory:
  path=Path(directory)/'test.zig'
  for index,case in enumerate(all_cases,1):
   path.write_text(case.source)
   if not args.reference_only:a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_defaults case {index} szc',timeout=120)
   b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_defaults case {index} zig',timeout=120)
   error=primary_error(b.stderr);errors=error_list(b.stderr)
   record=dict(index=index,label=case.label,lane=case.lane,source=case.source,diagnostics=case.diagnostics,zig_status=b.returncode,zig_error=error,zig_errors=errors,zig_stderr=b.stderr.decode(errors='replace'))
   if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):failures.append(f'reference assumption failed: {case.label}: {error}\n{record["zig_stderr"]}')
   if not args.reference_only:
    record.update(szc_status=a.returncode,szc_error=primary_error(a.stderr),szc_errors=error_list(a.stderr),szc_stderr=a.stderr.decode(errors='replace'))
    mismatch=a.returncode!=b.returncode or primary_error(a.stderr)!=error
    if case.diagnostics:mismatch |= error_list(a.stderr)!=errors
    if mismatch:failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+record['zig_stderr'])
   records.append(record)
   if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
 print(f'struct defaults: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
 if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
