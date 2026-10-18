#!/usr/bin/env python3
"""Ordinary local runtime structs; compile accept/exact primary parity, not runtime oracle.
No aggregate signatures containing structs, mutable global structs or volatile whole copies.
Native fixtures separately validate side effects and owned snapshots.
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
INIT='var s:S=.{.a=seed(1),.b=seed(2)}; '
def program(prefix,body):return prefix+' pub fn main() void {'+body+'}'
def cases():
    for label,body in (
        ('init-context',INIT+'s.a+=1;'),
        ('init-explicit','var s=S{.a=seed(1),.b=seed(2)}; s.b=9;'),
        ('init-as','var s=@as(S,.{.a=seed(1),.b=seed(2)}); s.a=9;'),
        ('whole-assign',INIT+'s=S{.a=seed(3),.b=seed(4)};'),
        ('self-assign',INIT+'s=s;'),
        ('contextual-overwrite',INIT+'s=.{.a=@intCast(s.b),.b=s.a};'),
        ('explicit-overwrite',INIT+'s=S{.a=@intCast(s.b),.b=s.a};'),
        ('as-overwrite',INIT+'s=@as(S,.{.a=@intCast(s.b),.b=s.a});'),
        ('temporary-overwrite',INIT+'const t:S=.{.a=@intCast(s.b),.b=s.a};s=t;'),
        ('value-snapshot',INIT+'const t=s; s.a=7; _=t.b;'),
        ('scalar-snapshot',INIT+'const t=s.a; s.a=7; _=t;'),
        ('dot-write',INIT+'s.a=7;s.b+=2;'),
        ('builtin-write',INIT+'@field(s,"a")=7;@field(s,"b")+=2;'),
        ('pointer-dot',INIT+'const p=&s;p.a=7;_=p.b;'),
        ('pointer-builtin',INIT+'const p=&s;@field(p,"a")=7;_= @field(p,"b");'),
        ('pointer-deref',INIT+'const p=&s; p.*.a=7;'),
        ('field-alias',INIT+'const p=&s.a;s.a=7;p.*+=1;'),
        ('qualified-read',INIT+'const p:*const S=&s;_=p.a;'),
        ('volatile-scalar',INIT+'const p:*volatile S=&s;p.a=7;_=p.b;'),
        ('allowzero-fields',INIT+'const p:*allowzero S=&s;p.a=7;_=p.b;'),
        ('under-aligned',INIT+'const p:*align(1) S=&s;p.b=513;_=p.b;'),
        ('all-flags',INIT+'const p:*align(1) volatile allowzero S=&s;p.b=513;_=p.b;'),
        ('root-overalign','var s:S align(64)=.{.a=seed(1),.b=2};s.b=9;_= &s;'),
        ('if-value',INIT+'const t:S=if(seed(1)==1)s else S{.a=3,.b=4};s.a=9;_=t.b;'),
        ('block-value',INIT+'const t:S=blk:{if(seed(1)==1)break :blk s;break :blk S{.a=3,.b=4};};s.a=9;_=t.b;'),
        ('if-assignment',INIT+'s=if(seed(1)==1)S{.a=3,.b=4} else s;'),
        ('undefined-whole','var s:S=undefined;s.a=seed(7);s.b=2;_=s.a;'),
        ('undefined-field','var s:S=.{.a=undefined,.b=seed(2)};s.a=7;'),
    ):yield Case(label,program(BASE,body))
    for label,prefix,body in (
        ('nested-snapshot','const I=struct{x:u8,};const O=struct{i:I,a:u8,};','var s:O=.{.i=.{.x=seed(1)},.a=2};const t=s.i;s.i.x=7;_=t.x;'),
        ('nested-context','const I=struct{x:u8,y:u8,};const O=struct{i:I,};','var s:O=.{.i=.{.x=seed(1),.y=2}};s=.{.i=.{.x=s.i.y,.y=s.i.x}};'),
        ('nested-address','const I=struct{x:u16,};const O=struct{i:I,};','var s:O=.{.i=.{.x=seed(1)}};const p=&s.i.x;p.*=9;'),
        ('array-field','const O=struct{a:[2]u8,};','var s:O=.{.a=.{seed(1),2}};s.a[seed(1)]=7;'),
        ('array-field-snapshot','const O=struct{a:[2]u8,};','var s:O=.{.a=.{seed(1),2}};const t=s.a;s.a[0]=7;_=t[0];'),
        ('array-of-structs','const I=struct{x:u8,};','var a=[_]I{.{.x=seed(1)},.{.x=2}};a[seed(1)].x=7;_=a[0].x;'),
        ('array-struct-snapshot','const I=struct{x:u8,};','var a=[_]I{.{.x=seed(1)},.{.x=2}};const t=a[seed(0)];a[0].x=7;_=t.x;'),
        ('nested-array-struct','const I=struct{x:u16,};const O=struct{a:[2]I,};','var s:O=.{.a=.{.{.x=seed(1)},.{.x=2}}};s.a[seed(1)].x=7;'),
        ('matrix-struct','const I=struct{x:u8,};','var a:[2][1]I=.{.{.{.x=seed(1)}},.{.{.x=2}}};a[seed(1)][0].x=7;'),
        ('slice-of-structs','const I=struct{x:u8,};','var a=[_]I{.{.x=seed(1)},.{.x=2}};const p:[]I=&a;p[seed(1)].x=7;'),
        ('slice-field','const O=struct{s:[]u8,};','var a=[_]u8{seed(1),2};var s:O=.{.s=&a};s.s[1]=7;s.s=s.s[0..1];'),
        ('slice-field-snapshot','const O=struct{s:[]u8,};','var a=[_]u8{seed(1),2};var s:O=.{.s=&a};const t=s;s.s=s.s[0..1];_=t.s.len;'),
        ('pointer-field','const O=struct{p:*u8,};','var a:u8=seed(1);var b:u8=2;var s:O=.{.p=&a};s.p.*=7;s.p=&b;'),
        ('sentinel-field','const O=struct{a:[2:9]u8,};','var s:O=.{.a=.{seed(1),2}};s.a[0]=7;_=s.a[2];'),
        ('zero-sentinel','const O=struct{a:[0:9]u8,x:u8,};','var s:O=.{.a=.{},.x=seed(1)};s.x=7;_=s.a[0];'),
        ('zero-size-fields','const E=struct{};const O=struct{a:void,b:u0,c:[0]u64,e:E,x:u8,};','var s:O=.{.a={},.b=0,.c=.{},.e=.{},.x=seed(1)};s.b=0;s.e=E{};s.x=7;_= &s.c;'),
        ('empty-runtime-local','const E=struct{};','var e:E=.{};e=E{};_= &e;'),
        ('odd-bool','const O=struct{n:u9,flag:bool,};','var s:O=.{.n=seed(1),.flag=true};s.n+=256;s.flag=false;'),
        ('quoted-name','const O=struct{@"two words":u8,};','var s:O=.{.@"two words"=seed(1)};@field(s,"two words")=7;'),
        ('single-nul-name','const O=struct{x:u8,};','var s:O=.{.x=seed(1)};@field(s,"x\\x00")=7;'),
    ):yield Case(label,program('noinline fn seed(x:u8)u8{return x;} '+prefix,body))
    for label,helpers,body in (
        ('dynamic-root','noinline fn root(p:*S)*S{return p;} ',INIT+'const p=root(&s);p.a=7;_=p.b;'),
        ('absolute-root','noinline fn address(p:*S)usize{return @intFromPtr(p);} ',INIT+'const p:*S=@ptrFromInt(address(&s));p.a=7;_=p.b;'),
        ('pointer-scalar-helper','noinline fn mutate(p:*S)u8{p.a=7;return p.a;} ',INIT+'_=mutate(&s);'),
        ('compound-once','noinline fn field(p:*S)*u8{return &p.a;} ',INIT+'field(&s).*+=2;'),
        ('builtin-dynamic','noinline fn root(p:*S)*S{return p;} ',INIT+'@field(root(&s),"a")=7;'),
    ):yield Case(label,program(BASE+helpers,body))
    for label,prefix,body in (
        ('const-write',BASE,'const s:S=.{.a=1,.b=2};s.a=7;'),
        ('const-pointer-write',BASE,INIT+'const p:*const S=&s;p.a=7;'),
        ('discard-const',BASE,INIT+'const p:*const S=&s;const q:*u8=&p.a;_=q;'),
        ('discard-volatile',BASE,INIT+'const p:*volatile S=&s;const q:*u8=&p.a;_=q;'),
        ('discard-allowzero',BASE,INIT+'const p:*allowzero S=&s;const q:*u8=&p.a;_=q;'),
        ('promote-alignment',BASE,INIT+'const p:*align(1) S=&s;const q:*u16=&p.b;_=q;'),
        ('missing',BASE,'var s:S=.{.a=seed(1)};s.a=2;'),
        ('duplicate',BASE,'var s:S=.{.a=seed(1),.a=2,.b=3};s.a=2;'),
        ('unknown',BASE,'var s:S=.{.a=seed(1),.b=2,.extra=3};s.a=2;'),
        ('overflow',BASE,'var s:S=.{.a=256,.b=seed(2)};s.a=2;'),
        ('bad-write-type',BASE,INIT+'s.a="bad";'),
        ('missing-projection',BASE,INIT+'s.nope=7;'),
        ('bad-field-name',BASE,INIT+'@field(s,3)=7;'),
        ('double-nul',BASE,INIT+'@field(s,"a\\x00\\x00")=7;'),
        ('array-oob','const O=struct{a:[2]u8,};noinline fn seed(x:u8)u8{return x;}','var s:O=.{.a=.{seed(1),2}};s.a[2]=7;'),
        ('nominal-mismatch','const A=struct{x:u8,};const B=struct{x:u8,};noinline fn seed(x:u8)u8{return x;}','var a:A=.{.x=seed(1)};const b:B=.{.x=seed(2)};a=b;'),
        ('missing-zero-field','const O=struct{x:u8,z:void,};noinline fn seed(x:u8)u8{return x;}','var s:O=.{.x=seed(1)};s.x=2;'),
    ):yield Case('error/'+label,program(prefix,body),'error')


    for label,body in (
        ('local-overflow-before-unknown','var s:S=.{.a=256,.unknown=1,.b=2};s.a=1;'),
        ('explicit-unknown-before-overflow','var s=S{.a=256,.unknown=1,.b=2};s.a=1;'),
        ('as-unknown-before-overflow','var s=@as(S,.{.a=256,.unknown=1,.b=2});s.a=1;'),
        ('overflow-before-missing','var s:S=.{.a=256};s.a=1;'),
        ('duplicate-before-overflow','var s:S=.{.a=256,.a=2,.b=3};s.a=1;'),
    ):yield Case('order/'+label,program(BASE,body),'error')

    absolute='const T=struct{wide:u64,byte:u8,};const p:*T=@ptrFromInt(4096); '
    yield Case('absolute/field-numeric-comptime',program(absolute,'const n:u8=if(@intFromPtr(&p.byte)==4096+@offsetOf(T,"byte"))0 else 256;_=n;'))
    yield Case('absolute/field-provenance',program(absolute,'const n:u8=if(@as(*anyopaque,@ptrCast(p))!=@as(*anyopaque,@ptrCast(&p.wide)))0 else 256;_=n;'))
    yield Case('absolute/field-flags',program('const T=struct{wide:u64,byte:u8,};const p:*align(16) volatile allowzero T=@ptrFromInt(4096); ','const q:*align(1) volatile allowzero T=p;const n:u8=if(@TypeOf(&p.wide)==*volatile allowzero u64 and @TypeOf(&q.wide)==*align(1) volatile allowzero u64)0 else 256;_=n;'))

    # Independent absolute-path/volatile edge captures; fake addresses are never executed.
    yield Case('edge/nested-absolute-scalar-read','const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const p:*const O=@ptrFromInt(4096); noinline fn read()u8{return p.inner.x;} pub fn main()void{_=read();}')
    yield Case('edge/absolute-array-index','const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const p:*const O=@ptrFromInt(4096);  pub fn main()void{const n:u8=if(@intFromPtr(&p.array[1])==4096+@offsetOf(O,"array")+@sizeOf(u16))0 else 256;_=n;}')
    yield Case('edge/absolute-array-struct-inner','const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const W=struct{a:u8,inner:I,};const p:*const [3]W=@ptrFromInt(4096); pub fn main()void{const n:u8=if(@intFromPtr(&p[1].inner.x)==4096+@sizeOf(W)+@offsetOf(W,"inner")+@offsetOf(I,"x"))0 else 256;_=n;}')
    yield Case('edge/near-max-formation','const T=struct{a:u64,b:u64,};const p:*const T=@ptrFromInt(18446744073709551608);pub fn main()void{const q=&p.b;_=q;}')
    yield Case('edge/near-max-numeric-wrap','const T=struct{a:u64,b:u64,};const p:*const T=@ptrFromInt(18446744073709551608);pub fn main()void{const n:u8=if(@intFromPtr(&p.b)==0)0 else 256;_=n;}')
    yield Case('edge/volatile-many-index-read','const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; noinline fn read(p:[*]const volatile I)u8{return p[0].x;}pub fn main()void{_=read(@ptrFromInt(4096));}')
    yield Case('edge/volatile-nested-read','const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; noinline fn read(p:*const volatile O)u8{return p.inner.x;}pub fn main()void{_=read(@ptrFromInt(4096));}')
    yield Case('error/runtime-struct-to-[0]u8','const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[0]u8=s;_=a;s.x=2;}','error')
    yield Case('error/runtime-struct-to-[1]u8','const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[1]u8=s;_=a;s.x=2;}','error')
    yield Case('error/runtime-struct-to-[]u8','const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[]u8=s;_=a;s.x=2;}','error')

    yield Case("absolute/nominal-field-base",'const A=struct {x:u8,};\nconst B=struct {x:u8,};\nconst a:*A=@ptrFromInt(4096);\nconst b:*B=@ptrFromInt(4096);\npub fn main()void{\nconst x:*u8=&a.x;\nconst y:*u8=&b.x;\nconst n:u8=if(x!=y)0 else 256;\n_=n;\n}\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc');parser.add_argument('zig')
    parser.add_argument('--reference-only',action='store_true');parser.add_argument('--report',type=Path)
    args=parser.parse_args();records=[];failures=[];all_cases=list(cases())
    if len(all_cases)>90:parser.error('90-case bound exceeded')
    with tempfile.TemporaryDirectory(prefix='szc-struct-runtime-') as directory:
        path=Path(directory)/'test.zig'
        for index,case in enumerate(all_cases,1):
            path.write_text(case.source)
            if not args.reference_only:a=run_compiler([args.szc,SZC_MODE,str(path)],case.source,f'diff_struct_runtime case {index} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],case.source,f'diff_struct_runtime case {index} zig',timeout=120)
            error=primary_error(b.stderr)
            records.append(dict(index=index,label=case.label,lane=case.lane,source=case.source,zig_status=b.returncode,zig_error=error,zig_stderr=b.stderr.decode(errors='replace')))
            if args.reference_only and (b.returncode!=(case.lane=='error') or (case.lane=='error' and error is None)):failures.append(f'reference assumption failed: {case.label}: {error}')
            if not args.reference_only and (a.returncode!=b.returncode or primary_error(a.stderr)!=error):failures.append('disagreement: '+case.label+'\n'+case.source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
    if args.report:args.report.write_text(json.dumps(records,indent=2)+'\n')
    print(f'struct runtime: {len(records)} cases; {sum(r["zig_status"]!=0 for r in records)} reference primary errors; {len(failures)} failures')
    if failures:raise SystemExit('\n'.join(failures))
if __name__=='__main__':main()
