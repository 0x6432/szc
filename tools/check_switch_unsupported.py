#!/usr/bin/env python3
"""Valid Zig switches explicitly NYI for the bounded integer/enum milestone.

All sources must first pass official Zig; candidate must then emit the stable
NYI prefix, not a language error, silent acceptance, crash or timeout.
"""
from diff_switch import Case, parser, run_cases

CASES = (
 ('pointer-capture', 'const U=union(enum){a:u8,b:u8};fn f(p:*U)u8{return switch(p.*){.a=>|*v| v.*,.b=>|*v| v.*};}export fn probe(x:u8)u8{var u:U=.{.a=x};return f(&u);}'),
 ('inline-prong', 'export fn probe(x:u8)u8{return switch(x){inline 1,2=>|v| v,else=>0};}'),
 ('labeled-redispatch', 'export fn probe(x:u8)u8{return again:switch(x){0=>continue :again 1,else=>|v| v};}'),
 ('bool', 'export fn probe(x:bool)u8{return switch(x){true=>1,false=>0};}'),
 ('pointer', 'export fn probe(x:*u8)u8{return switch(x){else=>|v| v.*};}'),
 ('type', 'export fn probe()u8{return switch(u8){u8=>1,else=>0};}'),
 ('tagged-union', 'const U=union(enum){a:u8,b:u16};fn f(x:U)u16{return switch(x){.a=>|v| v,.b=>|v| v};}export fn probe(x:u8)u16{return f(.{.a=x});}'),
 ('runtime-u128', 'export fn probe(lo:u64,hi:u64)u8{const x:u128=(@as(u128,hi)<<64)|lo;return switch(x){18446744073709551616=>1,else=>0};}'),
 ('nonexhaustive-underscore', 'const E=enum(u8){a=1,_};fn f(x:E)u8{return switch(x){.a=>1,_=>2};}export fn probe(x:u8)u8{return f(@enumFromInt(x));}'),
)

def main():
    p = parser()
    args = p.parse_args()
    selected = [Case('nyi/'+name, source+'\npub fn main()void{}\n', 'nyi', [24]) for name, source in CASES]
    if args.select: selected = [c for c in selected if any(s in c.label for s in args.select)]
    if not selected: p.error('no matching scope cases')
    return run_cases(args, selected, title='switch explicit NYI')

if __name__ == '__main__':
    raise SystemExit(main())
