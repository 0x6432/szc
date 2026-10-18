#!/usr/bin/env python3
"""Explicit enum scope gates; not diagnostic parity with official Zig."""
import argparse,json,tempfile
from pathlib import Path
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error
CASES=(
 ('signed-tag','const E=enum(i8){a=-1,b,c,};pub fn main()void{_=@sizeOf(E);}',),
 ('wide-tag','const E=enum(u65){a=1,b=18446744073709551617,};pub fn main()void{_=@sizeOf(E);}',),
 ('nonexhaustive','const E=enum(u8){a=1,_,};pub fn main()void{const e:E=@enumFromInt(7);_=e;}',),
 ('singleton-runtime','const E=enum{only,};fn f(n:u8)E{return @enumFromInt(n);}pub fn main()void{_=f(0);}',),
 ('singleton-storage','const E=enum{only,};var e:E=.only;pub fn main()void{const p:*E=&e;_=p;}',),
 ('C-export','const E=enum(u8){a,b,};export fn f(e:E)u8{return @intFromEnum(e);}pub fn main()void{}',),
 ('C-extern','const E=enum(u8){a,b,};extern fn f(e:E)u8;pub fn main()void{_=f(.a);}',),
 ('memory-memcpy','const E=enum(u8){a,b,};pub fn main()void{var a:[2]E=.{.a,.b};const b:[2]E=.{.b,.a};@memcpy(&a,&b);}',),
 ('memory-memset','const E=enum(u8){a,b,};pub fn main()void{var a:[2]E=.{.a,.b};@memset(&a,.b);}',),
 ('sentinel','const E=enum(u8){a,b,};pub fn main()void{const a:[2:.a]E=.{.b,.b};_= &a;}',),
 ('bitcast','const E=enum(u8){a=1,b=7,};pub fn main()void{const n:u8=@bitCast(E.a);_=n;}',),
 ('method','const E=enum(u8){a,b,fn get(e:E)u8{return @intFromEnum(e);}};pub fn main()void{const e:E=.a;_=e.get();}',),
 ('generic','const E=enum(u8){a,b,};fn f(e:anytype)u8{return @intFromEnum(e);}pub fn main()void{_=f(E.a);}',),
 ('comptime-call','const E=enum(u8){a,b,};fn f(e:E)u8{return @intFromEnum(e);}const n=f(.b);pub fn main()void{_=n;}',),
 ('explicit-callconv','const E=enum(u8){a,b,};fn f(e:E)callconv(.c)u8{return @intFromEnum(e);}pub fn main()void{_=f(.a);}',),
)
CONTRACT_ONLY=(
 ('C-varargs','const E=enum(u8){a,b,};extern fn f(...)void;pub fn main()void{f(E.a);}',),
)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('szc');p.add_argument('--reference-only',metavar='ZIG');p.add_argument('--report',type=Path);a=p.parse_args();bad=[];rows=[]
 with tempfile.TemporaryDirectory(prefix='szc-enum-scope-')as directory:
  path=Path(directory)/'test.zig'
  for i,(label,source)in enumerate(CASES+(CONTRACT_ONLY if a.reference_only else ()),1):
   path.write_text(source)
   if a.reference_only:cmd=[a.reference_only,'build-exe',str(path),'-fno-emit-bin','-j2'];who='zig'
   else:cmd=[a.szc,'emit-qbe',str(path)];who='szc'
   b=run_compiler(cmd,source,f'enum scope case {i} {who}',timeout=120);text=b.stderr.decode(errors='replace');rows.append(dict(index=i,label=label,source=source,status=b.returncode,primary=primary_error(b.stderr),stderr=text))
   if a.reference_only:
    if label=='C-varargs':
     if b.returncode!=1 or primary_error(b.stderr)!="expected type 'c_uint', found 'test.E'":bad.append(label+': unexpected contract-only diagnostic\n'+text)
    elif b.returncode:bad.append(label+': official rejected intended-valid boundary\n'+text)
   elif b.returncode!=1 or 'not supported by szc yet:' not in text:bad.append(label+': missing explicit unsupported gate\n'+text)
   if a.report:a.report.write_text(json.dumps(rows,indent=2)+'\n')
 print(f'enum scope: {len(rows)} gates; {len(bad)} failures')
 if bad:raise SystemExit('\n'.join(bad))
if __name__=='__main__':main()
