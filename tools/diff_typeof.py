#!/usr/bin/env python3
"""Compare unevaluated @TypeOf and supported peer types with Zig."""
import argparse
from pathlib import Path
import tempfile
from compiler_process import run_compiler
from diff_thin_pointer_casts import primary_error

SZC_MODE = "emit-qbe"


def cases():
    for ty in ('u0','i0','u8','i8','u33','i40','u64','u128','bool','void','type',
               'f32','f64','*u8','*align(32) const volatile u8','[*]const u8',
               '[3]u8','[0]u0','[2][3]u16','[]u16','[:0]const u8'):
        yield f'pub fn main() void {{ const n:u8=if(@TypeOf(@as({ty},undefined))=={ty})0 else 256; _=n; }}'
    for expr,ty in (('3','comptime_int'),('true','bool'),('u8','type'),('struct {}','type'),
                    ("'a'",'comptime_int'),('"abc"','*const [3:0]u8'),('{}','void'),
                    ('@sizeOf(u33)','comptime_int'),('@hasField([]u8,"ptr")','bool')):
        yield f'pub fn main() void {{ const n:u8=if(@TypeOf({expr})=={ty})0 else 256; _=n; }}'
    for expr in ('undefined','null','@TypeOf(undefined)','"a"++"bc"',
                 '@as(*const [3]u8,"abc")','@as([]const u8,"abc")'):
        yield f'pub fn main() void {{ _=@TypeOf({expr}); }}'
    for a in ('u0','i0','u8','i8','u16','i16','u33','i40'):
        for b in ('u0','i0','u8','i8','u16','i16','u33','i40'):
            signed = [t for t in (a,b) if t.startswith('i')]
            unsigned = [t for t in (a,b) if t.startswith('u')]
            expected = max((a,b), key=lambda t:int(t[1:]))
            if signed and unsigned and int(signed[0][1:]) == int(unsigned[0][1:]):
                expected = a
            yield f'pub fn main() void {{ const n:u8=if(@TypeOf(@as({a},undefined),@as({b},undefined))=={expected})0 else 256; _=n; }}'
    for args,ty in (('@as(u8,undefined),@as(i8,undefined),@as(i16,undefined)','i16'),
                    ('@as(i16,undefined),@as(u16,undefined),1','i16'),
                    ('@as(u16,undefined),@as(i16,undefined),1','u16'),
                    ('@as(f32,undefined),@as(f64,undefined)','f64'),
                    ('@as(f32,undefined),@as(u64,undefined)','f32'),
                    ('@as(comptime_int,undefined),@as(u8,1)','u8')):
        yield f'pub fn main() void {{ const n:u8=if(@TypeOf({args})=={ty})0 else 256; _=n; }}'
    for args in ('@as(u8,1),256','256,@as(u8,1)','undefined,@as(u8,1)',
                 '@as(u8,1),undefined','1,2,3','u8,u16,bool','true,false,true',
                 '@as(u8,1),@as(u16,2),3','1,@as(u8,2),@as(u16,3)',
                 '"a","abc"','"a",@as([]const u8,"abc")',
                 '@as(*u8,undefined),@as(*const u8,undefined)',
                 '@as(*align(32) volatile u8,undefined),@as(*const u8,undefined)',
                 '@as([*]u8,undefined),@as(*[3]u8,undefined)',
                 '@as([2]u8,undefined),@as([3]u8,undefined)',
                 '1,true','u8,1','@as(u8,1),@as([]u8,undefined)'):
        yield f'pub fn main() void {{ _=@TypeOf({args}); }}'
    for a,b in (('u8','i8'),('i8','u8'),('u16','i8'),('i16','u8'),
                ('u8','u16'),('i8','i16'),('u0','i0')):
        for suffix in ('',',@as(u8,1)',',@as(i16,1)'):
            yield (f'fn f(a:{a},b:{b}) void {{ _=@TypeOf(a,b{suffix}); }} '
                   'pub fn main() void { f(0,0); }')
    for integer in ('u24','u25','i25','i26','u53','u54','u64'):
        for floating in ('f32','f64'):
            yield (f'fn f(a:{integer}) void {{ _=@TypeOf(a,@as({floating},undefined)); }} '
                   'pub fn main() void { f(0); }')
    for args,ty in (('undefined,&f,@as(*const fn() u8,undefined)','*const fn() u8'),
                    ('undefined,f,@as(*const fn() u8,undefined)','*const fn() u8'),
                    ('undefined,@as(*u8,undefined),@as(*const u8,undefined)','*const u8'),
                    ('"abc","a","abcde"','[:0]const u8'),
                    ('@as(comptime_int,undefined),@as(comptime_int,undefined)','comptime_int')):
        yield ('fn f() u8 { return 1; } pub fn main() void { '
               f'const n:u8=if(@TypeOf({args})=={ty})0 else 256; _=n; }}')
    for expr,ty in (('a+1','u8'),('a+b','u16'),('p.*','u8'),('s.len','usize'),
                    ('s.ptr','[*]const u8'),('s[0]','u8'),('s[0..a]','[]const u8'),
                    ('@field(s,"len")','usize'),('@TypeOf(a)','type')):
        yield ('fn f(a:u8,b:u16,p:*volatile u8,s:[]const u8) void { '
               f'const n:u8=if(@TypeOf({expr})=={ty})0 else 256; _=n; '
               '_=@TypeOf(a); _=@TypeOf(b); _=@TypeOf(p); _=@TypeOf(s); } '
               'pub fn main() void { var x:u8=1; f(1,2,&x,"abc"); }')
    for expr in ('value()','array()','&value','@field(N,"x")',
                 '@TypeOf(value())','@TypeOf(array()),@TypeOf(value())'):
        yield ('var count:u8=0; const N=struct { const x:u8=7; }; '
               'fn value() u8 { count+=1; return 7; } '
               'fn array() [2]u8 { count+=1; return .{1,2}; } '
               f'pub fn main() void {{ _=@TypeOf({expr}); }}')
    for expr in ('a','a[0]','a[0..]','&a','a[0..1]'):
        yield f'fn f(a:[2]u8) void {{ _=@TypeOf({expr}); }} pub fn main() void {{ f(.{{1,2}}); }}'
    for expr in ('bad','bad,@as(*const fn() u8,undefined)',
                 '@as(*const fn() u8,undefined),bad',
                 'undefined,bad,@as(*const fn() u8,undefined)'):
        yield f'fn bad() u8 {{ return 256; }} pub fn main() void {{ _=@TypeOf({expr}); }}'
    for expr in ('bad()','&bad'):
        yield f'fn bad() u8 {{ return 256; }} pub fn main() void {{ _=@TypeOf({expr}); }}'
    for decl,expr in (('const x:u8=256;','x'),('var x:u8=256;','x'),('var x:u8=256;','&x')):
        yield decl+f'pub fn main() void {{ _=@TypeOf({expr}); }}'
    for expr in ('@as(u8,255)+1','@as(u8,1)/0','@as(*u8,undefined).*',
                 '@as(*const fn() u8,undefined)()','unreachable','return',
                 '@as(u8,1),unreachable','unreachable,@as(u8,1)'):
        yield f'pub fn main() void {{ _=@TypeOf({expr}); }}'
    yield 'pub fn main() void { _=@TypeOf(); }'
    yield 'pub fn main() void { out: { _=@TypeOf(break :out); } }'
    yield 'pub fn main() void { while(true) { _=@TypeOf(continue); } }'
    yield 'extern fn f(a:[2]u8) [2]u8; pub fn main() void { _=@TypeOf(f(.{1,2})); }'
    yield 'fn f(b:bool) u8 { _=@TypeOf(if(b) return 2 else @as(u8,3)); return 0; } pub fn main() void { _=f(true); }'
    yield 'fn f(b:bool) void { outer: { _=@TypeOf(if(b) break :outer else @as(u8,3)); } } pub fn main() void { f(true); }'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc')
    parser.add_argument('zig')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='szc-typeof-') as directory:
        path = Path(directory) / 'test.zig'
        diagnostics = 0
        for checked,source in enumerate(cases(),1):
            path.write_text(source)
            a=run_compiler([args.szc,SZC_MODE,str(path)],source,f'diff_typeof case {checked} szc',timeout=120)
            b=run_compiler([args.zig,'build-exe',str(path),'-fno-emit-bin','-j2'],source,f'diff_typeof case {checked} zig',timeout=120)
            error=primary_error(b.stderr)
            if a.returncode != b.returncode or primary_error(a.stderr) != error:
                raise SystemExit('disagreement:\n'+source+'\n'+a.stderr.decode(errors='replace')+b.stderr.decode(errors='replace'))
            diagnostics += error is not None
        print(f'TypeOf: {checked} matched Zig 0.17.0; {diagnostics} exact primary diagnostics',flush=True)


if __name__ == '__main__':
    main()
