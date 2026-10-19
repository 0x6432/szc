#!/usr/bin/env python3
"""Runtime panic regressions, with optional checks against the official Zig binary."""
import argparse
import os
import resource
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


# Share the exact timeout lifecycle used by the compiler comparison tools.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run


def binary_case(name, integer_type, operator, lhs, rhs, message="integer overflow"):
    source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{\n"
              f"    return a {operator} b;\n}}\n"
              f"pub fn main() void {{ _ = operation({lhs}, {rhs}); }}\n")
    return name, source, message


def cases():
    # Exhaustive membership is checked in the original source integer domain.
    for label, source_type, destination, tags, value in (
        ('hole', 'u16', 'u8', 'one=1,seven=7,last=250', '2'),
        ('width', 'u16', 'u8', 'one=1,seven=7,last=250', '257'),
        ('negative', 'i16', 'u8', 'one=1,seven=7,last=250', '-1'),
        ('high_low_alias', 'u64', 'u64', 'one=1,high=9223372036854775815,last=18446744073709551615', '9223372036854775809'),
        ('max_hole', 'u64', 'u64', 'one=1,high=9223372036854775815,last=18446744073709551615', '18446744073709551614'),
    ):
        yield 'enum_invalid_' + label, (
            f'const E=enum({destination}){{{tags}}}; '
            f'noinline fn operation(n:{source_type})E{{return @enumFromInt(n);}} '
            f'pub fn main()void{{_=operation({value});}}\n'), 'invalid enum value'
    prefix = 'const S=struct { a:u8,b:u64,c:u16, }; '
    yield 'struct_pointer_alignment', prefix + (
        'fn operation(n:usize) void { const p:*S=@ptrFromInt(n); _=p; } '
        'pub fn main() void { operation(4097); }'), 'incorrect alignment'
    yield 'struct_align_cast', prefix + (
        'fn operation(p:*align(1) S) void { const q:*S=@alignCast(p); _=q; } '
        'pub fn main() void { operation(@ptrFromInt(4097)); }'), 'incorrect alignment'
    yield 'struct_slice_divisibility', prefix + (
        'fn operation(s:[]u8) void { const q:[]align(1) S=@ptrCast(s); _=q; } '
        'pub fn main() void { const p:[*]u8=@ptrFromInt(4096); operation(p[0..17]); }'), (
        'slice length does not divide exactly into destination elements'), (
        'does not divide exactly into destination elements')
    for leaf in ('type', 'u128'):
        yield 'memcpy_empty_array_length_'+leaf, (
            f'fn operation(d:[][0]{leaf},s:[]const [0]{leaf}) void {{ @memcpy(d,s); }} '
            f'pub fn main() void {{ var a:[2][0]{leaf}=undefined; '
            f'const b:[1][0]{leaf}=.{{.{{}}}}; operation(&a,&b); }}\n'), (
            'source and destination arguments have non-equal lengths')
    yield 'memset_aggregate_destination_bounds', (
        'fn operation(d:[][2]u8,n:usize) void { @memset(d[0..n],.{1,2}); } '
        'pub fn main() void { var a:[2][2]u8=undefined; operation(&a,3); }\n'), 'index out of bounds'
    yield 'memset_aggregate_fill_bounds', (
        'fn operation(d:[][2]u8,s:[]const [2]u8,n:usize) void { @memset(d,s[n]); } '
        'pub fn main() void { var a:[2][2]u8=undefined; const b:[1][2]u8=.{.{1,2}}; operation(&a,&b,1); }\n'), 'index out of bounds'
    for label, ty, initial in (('array', '[3]u8', '.{1,2,3}'), ('descriptor', '[]const u8', '"abc"')):
        yield 'memcpy_aggregate_alias_'+label, (
            f'fn operation(d:[]{ty},s:[]const {ty}) void {{ @memcpy(d,s); }} '
            f'pub fn main() void {{ var a:[3]{ty}=.{{{initial},{initial},{initial}}}; operation(a[0..2],a[1..3]); }}\n'), (
            '@memcpy arguments alias')
    yield 'memcpy_aggregate_length', (
        'fn operation(d:[][3]u8,s:[]const [3]u8) void { @memcpy(d,s); } '
        'pub fn main() void { var a:[2][3]u8=undefined; const b:[1][3]u8=.{.{1,2,3}}; operation(&a,&b); }\n'), (
        'source and destination arguments have non-equal lengths')
    for ty, value in (('u8', '7'), ('u0', '0'), ('void', '{}')):
        for d, n in ((0, 1), (2, 1)):
            yield f'memcpy_length_{ty}_{d}_{n}', (
                f'fn operation(d:[]{ty},s:[]const {ty}) void {{ @memcpy(d,s); }} '
                f'pub fn main() void {{ var a:[{d}]{ty}=undefined; '
                f'const b:[{n}]{ty}=.{{{value}}}; operation(&a,&b); }}\n'), (
                'source and destination arguments have non-equal lengths')
    for lo, hi, n in ((0,0,1), (0,1,2), (1,0,2)):
        for ty in ('u8', 'u33'):
            yield f'memcpy_alias_{ty}_{lo}_{hi}', (
                f'fn operation(p:[]{ty},a:usize,b:usize,n:usize) void {{ @memcpy(p[a..][0..n],p[b..][0..n]); }} '
                f'pub fn main() void {{ var a:[3]{ty}=.{{7,9,11}}; operation(&a,{lo},{hi},{n}); }}\n'), (
                '@memcpy arguments alias')
    yield 'memcpy_length_before_alias', (
        'fn operation(d:[]u8,s:[]const u8) void { @memcpy(d,s); } '
        'pub fn main() void { var a:[2]u8=.{1,2}; operation(&a,a[0..1]); }\n'), (
        'source and destination arguments have non-equal lengths')
    for label, source, message in (
        ("parameter", 'fn crash(s: []const u8) noreturn { @panic(s); } pub fn main() void { crash("dynamic message"); }', "dynamic message"),
        ("binary", 'fn crash(s: []const u8) noreturn { @panic(s); } pub fn main() void { var a = [4]u8{65,0,255,66}; a[3] = 67; crash(&a); }', b"A\x00\xffC"),
        ("global", 'var s: []const u8 = "old"; pub fn main() void { s = "longer message"; @panic(s); }', "longer message"),
        ("empty", 'fn crash(s: []const u8) noreturn { @panic(s); } pub fn main() void { crash(""); }', ""),
        ("join_short", 'fn crash(c: bool) noreturn { @panic(if (c) "x" else "long message"); } pub fn main() void { crash(true); }', "x"),
        ("join_long", 'fn crash(c: bool) noreturn { @panic(if (c) "x" else "long message"); } pub fn main() void { crash(false); }', "long message"),
        ("mixed_join", 'fn crash(c: bool, s: []const u8) noreturn { @panic(if (c) s else "tail"); } pub fn main() void { crash(true,"borrowed"); }', "borrowed"),
        ("cleanup", 'fn crash(s: []const u8) noreturn { defer @panic(s); } pub fn main() void { crash("cleanup slice"); }', "cleanup slice"),
    ):
        yield "panic_slice_" + label, source + "\n", message

    for label, source in (
        ('thin', 'fn f(p: *u8) void { const q: *align(16) u8 = @alignCast(p); _ = q; } pub fn main() void { f(@ptrFromInt(17)); }'),
        ('allowzero', 'fn f(p: *allowzero u8) void { const q: *align(16) allowzero u8 = @alignCast(p); _ = q; } pub fn main() void { f(@ptrFromInt(17)); }'),
        ('many', 'fn f(p: [*]u8) void { const q: [*]align(16) u8 = @alignCast(p); _ = q; } pub fn main() void { f(@ptrFromInt(17)); }'),
        ('array', 'fn f(p: *[2]u8) void { const q: *align(16) [2]u8 = @alignCast(p); _ = q; } pub fn main() void { f(@ptrFromInt(17)); }'),
        ('slice', 'fn f(p: []u8) void { const q: []align(16) u8 = @alignCast(p); _ = q; } pub fn main() void { const p: [*]u8 = @ptrFromInt(17); f(p[0..1]); }'),
        ('combined', 'fn f(p: *const volatile u8) void { const q: *align(16) u8 = @constCast(@alignCast(@volatileCast(p))); _ = q; } pub fn main() void { f(@ptrFromInt(17)); }'),
        ('local_offset', 'var a: [2]u8 align(64) = .{7,9}; fn f(p: *u8) void { const q: *align(16) u8 = @alignCast(p); _ = q; } pub fn main() void { f(&a[1]); }'),
    ):
        yield 'align_cast_'+label, source+'\n', 'incorrect alignment'

    for label, source_type, destination, expression, address, message in (
        ('slice_null', '*allowzero [4]u8', '[]u16', '@alignCast(@ptrCast(p))', 0, 'cast causes pointer to be null'),
        ('slice_alignment', '*[4]u8', '[]u16', '@alignCast(@ptrCast(p))', 17, 'incorrect alignment'),
        ('empty_array_null', '*allowzero [0]u8', '[]u8', '@ptrCast(p)', 0, 'cast causes pointer to be null'),
        ('empty_array_alignment', '*[0]u8', '[]u16', '@alignCast(@ptrCast(p))', 17, 'incorrect alignment'),
        ('null', '*allowzero u8', '*i8', '@ptrCast(p)', 0, 'cast causes pointer to be null'),
        ('null_many', '[*]allowzero u8', '[*:17]i8', '@ptrCast(p)', 0, 'cast causes pointer to be null'),
        ('null_before_alignment', '*allowzero u8', '*u16', '@alignCast(@ptrCast(p))', 0, 'cast causes pointer to be null'),
        ('alignment', '*u8', '*u16', '@alignCast(@ptrCast(p))', 17, 'incorrect alignment'),
        ('nullable_alignment', '*allowzero u8', '*allowzero u16', '@alignCast(@ptrCast(p))', 17, 'incorrect alignment'),
        ('combined', '*const volatile u8', '*u32', '@volatileCast(@constCast(@alignCast(@ptrCast(p))))', 17, 'incorrect alignment'),
    ):
        yield 'thin_ptr_cast_' + label, (
            f'fn f(p: {source_type}) void {{ const q: {destination} = {expression}; _ = q; }} '
            f'pub fn main() void {{ f(@ptrFromInt({address})); }}\n'), message

    for label, source_type, destination, expression, address, message in (
        ('empty_null', '[]allowzero u8', '*i8', '@ptrCast(s)', 0, 'cast causes pointer to be null'),
        ('empty_alignment', '[]u8', '*u16', '@alignCast(@ptrCast(s))', 17, 'incorrect alignment'),
        ('empty_many_alignment', '[]u8', '[*]u16', '@alignCast(@ptrCast(s))', 17, 'incorrect alignment'),
        ('empty_nullable_alignment', '[]allowzero u8', '*allowzero u16', '@alignCast(@ptrCast(s))', 17, 'incorrect alignment'),
    ):
        many_type = '[*]allowzero u8' if 'allowzero' in source_type else '[*]u8'
        yield 'slice_ptr_cast_' + label, (
            f'fn f(s: {source_type}) void {{ const p: {destination} = {expression}; _ = p; }} '
            f'pub fn main() void {{ const p: {many_type} = @ptrFromInt({address}); f(p[0..0]); }}\n'), message

    for label, source_type, destination, address, message in (
        ('null', '[]allowzero u8', '[]i8', 0, 'cast causes pointer to be null'),
        ('null_before_alignment', '[]align(1) allowzero u16', '[]u16', 0, 'cast causes pointer to be null'),
        ('alignment', '[]align(1) u16', '[]u16', 17, 'incorrect alignment'),
        ('nullable_alignment', '[]align(1) allowzero u16', '[]allowzero u16', 17, 'incorrect alignment'),
    ):
        many_type = source_type.replace('[]', '[*]', 1)
        yield 'equal_slice_cast_' + label, (
            f'fn f(s: {source_type}) void {{ const p: {destination} = @alignCast(@ptrCast(s)); _ = p; }} '
            f'pub fn main() void {{ const p: {many_type} = @ptrFromInt({address}); f(p[0..1]); }}\n'), message

    for label, source_type, destination, count, address, message in (
        ('zero_bit_null', '[]allowzero u0', '[]u8', 3, 0, 'cast causes pointer to be null'),
        ('zero_bit_alignment', '[]u0', '[]u16', 3, 17, 'incorrect alignment'),
        ('remainder', '[]u8', '[]u16', 3, 16, 'slice length does not divide exactly into destination elements'),
        ('odd_source', '[][3]u8', '[]u16', 1, 16, 'slice length does not divide exactly into destination elements'),
        ('odd_destination', '[]u16', '[][3]u8', 1, 16, 'slice length does not divide exactly into destination elements'),
        ('null_first', '[]allowzero u8', '[]u16', 3, 0, 'cast causes pointer to be null'),
        ('alignment_first', '[]u8', '[]u16', 3, 17, 'incorrect alignment'),
    ):
        many_type = source_type.replace('[]', '[*]', 1)
        source = (
            f'fn f(s: {source_type}) void {{ const p: {destination} = @alignCast(@ptrCast(s)); _ = p; }} '
            f'pub fn main() void {{ const p: {many_type} = @ptrFromInt({address}); f(p[0..{count}]); }}\n')
        reference_message = 'does not divide exactly into destination elements' if 'slice length' in message else message
        yield 'resized_slice_cast_' + label, source, message, reference_message

    for label, ty, address, message in (
        ('null', '*u8', 0, 'cast causes pointer to be null'),
        ('aligned_null', '*u32', 0, 'cast causes pointer to be null'),
        ('alignment', '*u32', 1, 'incorrect alignment'),
        ('allowzero_alignment', '*allowzero u32', 1, 'incorrect alignment'),
        ('overaligned', '*align(256) u8', 16, 'incorrect alignment'),
    ):
        yield 'ptr_from_int_'+label, (
            f'fn operation(n: usize) void {{ const p: {ty} = @ptrFromInt(n); _ = p; }} '
            f'pub fn main() void {{ operation({address}); }}\n'), message

    for label, child, values, sentinel, end in (
        ('array', 'u16', '7,9,18', '17', 2),
        ('boolean', 'bool', 'true,true,false', 'true', 2),
        ('signed', 'i16', '7,9,-18', '-17', 2),
        ('wide', 'i33', '7,9,-18', '-17', 2),
        ('empty', 'u16', '18,9,17', '17', 0),
    ):
        for kind, ty in (('array', f'*[3]{child}'), ('slice', f'[]{child}'), ('many', f'[*]{child}')):
            source = (f'fn operation(p: {ty}, n: usize) void {{ _ = p[0..n:{sentinel}]; }} '
                      f'pub fn main() void {{ var a = [3]{child}{{{values}}}; operation(&a,{end}); }}\n')
            yield 'sentinel_assertion_'+label+'_'+kind, source, 'sentinel mismatch'
    yield 'sentinel_assertion_bounds', (
        'fn operation(p: []u16, n: usize) void { _ = p[0..n:17]; } '
        'pub fn main() void { var a = [3]u16{7,9,17}; operation(&a,3); }\n'), 'index out of bounds'
    yield 'sentinel_assertion_order', (
        'fn operation(p: []u16, first: usize, last: usize) void { _ = p[first..last:17]; } '
        'pub fn main() void { var a = [3]u16{7,9,17}; operation(&a,2,1); }\n'), (
        'start index is larger than end index'), 'start index 2 is larger than end index 1'

    for label, expression, index in (
        ('read', '_ = s[n]', 4),
        ('write', 's[n] = 9', 4),
        ('window', '_ = s[0..n]', 5),
        ('tail', '_ = s[n..]', 4),
    ):
        source = ('fn operation(s: [:17]u16, n: usize) void { '+expression+'; } '
                  'pub fn main() void { var a: [3:17]u16 = .{7,9,11}; operation(&a,'+str(index)+'); }\n')
        message = 'index out of bounds' if label != 'tail' else 'start index is larger than end index'
        reference_message = 'start index 4 is larger than end index 3' if label == 'tail' else message
        yield 'sentinel_slice_'+label, source, message, reference_message

    yield 'sentinel_slice_zero_bits', (
        'fn operation(s: [:0]const u0, n: usize) void { _ = s[n]; } '
        'pub fn main() void { const a = [2:0]u0{0,0}; operation(&a,100); }\n'), 'index out of bounds'

    yield 'sentinel_slice_known_window', (
        'const a = [3:17]u16{7,9,11}; const s: [:17]const u16 = &a; '
        'fn operation(n: usize) void { _ = s[n..4]; } pub fn main() void { operation(1); }\n'), 'index out of bounds'

    for label, child, sentinel, count, values, index in (
        ('integer', 'u16', '17', 3, '7,9,11', 4),
        ('boolean', 'bool', 'true', 2, 'false,true', 3),
        ('empty', 'u16', '99', 0, '', 1),
    ):
        source = (f'fn operation(p: *const [{count}:{sentinel}]{child}, n: usize) void {{ _ = p[n]; }} '
                  f'pub fn main() void {{ const a = [{count}:{sentinel}]{child}{{{values}}}; operation(&a,{index}); }}\n')
        yield 'sentinel_array_'+label, source, 'index out of bounds'
    yield 'sentinel_array_window', ('fn operation(p: *const [3:17]u16, n: usize) void { _ = p[0..n]; } '
                                   'pub fn main() void { const a = [3:17]u16{7,9,11}; operation(&a,5); }\n'), 'index out of bounds'

    for label, operation in (
        ('shrink_index', 's.len = 0; _ = s[0];'),
        ('advance_index', 's.ptr += 2; s.len = 1; _ = s[1];'),
        ('shrink_window', 's.len = 1; _ = s[0..2];'),
    ):
        source = ('pub fn main() void { var a = [3]u8{7,8,9}; var s: []u8 = &a; '
                  +operation+' }\n')
        yield 'slice_field_'+label, source, 'index out of bounds'

    for label, ty in (('array', '*[3]u8'), ('slice', '[]u8'), ('many', '[*]u8')):
        for lo, hi in ((2,1), (4,3), (0,4), (4,4)):
            if ty == '[*]u8' and lo <= hi:
                continue
            message = 'start index is larger than end index' if lo > hi else 'index out of bounds'
            source = (f'fn operation(p: {ty}, lo: usize, hi: usize) void {{ _ = p[lo..hi]; }} '
                      f'pub fn main() void {{ var a = [3]u8{{7,8,9}}; operation(&a,{lo},{hi}); }}\n')
            reference_message = f'start index {lo} is larger than end index {hi}' if lo > hi else message
            yield f'slicing_{label}_{lo}_{hi}', source, message, reference_message
    for child, value in (('u0','0'), ('void','{}')):
        source = (f'fn operation(p: []const {child}, n: usize) void {{ _ = p[0..n]; }} '
                  f'pub fn main() void {{ const a = [2]{child}{{{value},{value}}}; operation(&a,3); }}\n')
        yield 'slicing_zero_bits_'+child, source, 'index out of bounds'

    for n, m in ((1, 2), (0, 1)):
        source = ("fn operation(a: []const u8, b: []const u8) void { for (a,b) |_,_| {} } "
                  f"pub fn main() void {{ operation(&[{n}]u8{{" + ','.join(['7']*n)
                  + f"}}, &[{m}]u8{{" + ','.join(['9']*m) + "}); }\n")
        yield f"for_slice_mismatch_{n}_{m}", source, "for loop over objects with non-equal lengths"
    source = ("fn operation(a: []const u8, n: usize) void { for (a,0..n) |_,_| {} } "
              'pub fn main() void { operation("hi",1); }\n')
    yield "for_slice_range_mismatch", source, "for loop over objects with non-equal lengths"

    for child, value in (("u8", "7"), ("u0", "0"), ("void", "{}")):
        # Reads of one-possible-value children may fold upstream without a guard;
        # address formation must retain the slice bounds check for every child.
        source = (f"fn operation(s: []const {child}, n: usize) void {{ _ = &s[n]; }} "
                  f"pub fn main() void {{ const a = [2]{child}{{{value},{value}}}; operation(&a,2); }}\n")
        yield "slice_address_"+child, source, "index out of bounds"
    for operation in ("_ = s[n];", "s[n] = 3;"):
        source = ("fn operation(s: []u8, n: usize) void { "+operation+" } "
                  "pub fn main() void { var a = [2]u8{7,9}; operation(&a,2); }\n")
        yield "slice_"+("read" if operation.startswith("_") else "store"), source, "index out of bounds"

    for label, operation in (("read", "_ = a[n];"), ("store", "a[n] = 3;"),
                             ("address", "_ = &a[n];"), ("nested", "_ = a[0][n];")):
        ty = "[2][2]u8" if label == "nested" else "[2]u8"
        value = ".{.{1,2},.{3,4}}" if label == "nested" else ".{1,2}"
        source = (f"var a: {ty} = {value}; fn operation(n: usize) void {{ {operation} }} "
                  "pub fn main() void { operation(2); }\n")
        yield "global_array_" + label, source, "index out of bounds"
    yield "array_callback_noreturn", 'fn stop(a: u8, b: u8, c: u8, d: u8, e: u8, f: u8, g: u8, h: u8) noreturn {\n    if (a != 1 or b != 2 or c != 3 or d != 4 or e != 5 or f != 6 or g != 7 or h != 8) @panic("wrong array callback arguments");\n    @panic("array callback terminated");\n}\nfn invoke(p: *const fn (u8, u8, u8, u8, u8, u8, u8, u8) [2]u8) void { _ = p(1,2,3,4,5,6,7,8); }\npub fn main() void { invoke(&stop); }\n', "array callback terminated"
    for name, operation in (
        ("store", "a[n] = 3;"),
        ("address", "const p = &a[n]; p.* = 3;"),
        ("zero_store", "var z: [2]u0 = undefined; z[n] = 0; _ = &a;"),
        ("nested", "var z: [2][2]u8 = .{.{1,2},.{3,4}}; z[0][n] = 3; _ = &a;"),
    ):
        source = ("fn operation(n: usize) void { var a = [_]u8{1,2}; " + operation + " }\n"
                  "pub fn main() void { operation(2); }\n")
        yield "array_mutable_" + name, source, "index out of bounds"
    for child, value in (("u8", "7"), ("i33", "-7"), ("u0", "0"), ("void", "{}")):
        for index in ("2", "18446744073709551615"):
            source = (f"fn read(p: *const [2]{child}, n: usize) {child} {{ return p[n]; }}\n"
                      f"pub fn main() void {{ const a = [_]{child}{{{value}, {value}}}; _ = read(&a, {index}); }}\n")
            yield f"array_pointer_{child}_{index}", source, "index out of bounds"
    yield "array_value_index", ("fn read(n: usize) u8 { const a = [_]u8{7,9}; return a[n]; }\n"
                                "pub fn main() void { _ = read(2); }\n"), "index out of bounds"
    yield "array_string_index", ("fn read(n: usize) u8 { return \"hi\"[n]; }\n"
                                 "pub fn main() void { _ = read(3); }\n"), "index out of bounds"
    yield "function_pointer_noreturn_coercion", ('fn stop() noreturn { @panic("coerced callback"); }\n'
          'fn invoke(p: *const fn () void) void { p(); }\n'
          'pub fn main() void { invoke(&stop); }\n'), "coerced callback"
    yield "function_pointer_noreturn", ('fn stop() noreturn { @panic("callback"); }\n'
          'fn invoke(p: *const fn () noreturn) noreturn { p(); }\n'
          'pub fn main() void { invoke(&stop); }\n'), "callback"
    yield "function_pointer_overflow", ('fn operation(n: u8) u8 { return n + 1; }\n'
          'fn invoke(p: *const fn (u8) u8) void { _ = p(255); }\n'
          'pub fn main() void { invoke(&operation); }\n'), "integer overflow"
    for label, body, message in (
        ("overflow", "n += 1;", "integer overflow"),
        ("cast", "const x: u4 = @intCast(n); _ = x;", "integer does not fit in destination type"),
        ("cleanup", "defer n += 1;", "integer overflow"),
    ):
        yield f"global_{label}", f"var n: u8 = 255;\npub fn main() void {{ {body} }}\n", message
    yield "panic_global", 'var text = "first";\npub fn main() void { text = "later"; @panic(text); }\n', "later"
    for label, body, message in (
        ("overflow", "p.* += 1;", "integer overflow"),
        ("cast", "const n: u4 = @intCast(p.*); _ = n;", "integer does not fit in destination type"),
        ("cleanup", "defer p.* += 1;", "integer overflow"),
    ):
        source = (f"fn operation(p: *u8) void {{ {body} }}\n"
                  "pub fn main() void { var n: u8 = 255; operation(&n); }\n")
        yield f"pointer_{label}", source, message
    yield "panic_reference", 'pub fn main() void { @panic((&"reference").*); }\n', "reference"
    source = ('fn operation(p: *const u8) u4 { return @intCast(p.*); }\n'
              'pub fn main() void { const n: u8 = 255; _ = operation(&n); }\n')
    yield "constant_pointer_cast", source, "integer does not fit in destination type"
    for label, literal, message in (
        ("plain", '"explicit panic"', "explicit panic"),
        ("empty", '""', ""),
        ("escapes", r'"quote \" slash \\ tab \t CR \r LF \n"', 'quote " slash \\ tab \t CR \r LF \n'),
        ("utf8", '"héllo 😀"', "héllo 😀"),
        ("unicode", r'"\u{0}\u{7f}\u{80}\u{7ff}\u{800}\u{d7ff}\u{e000}\u{ffff}\u{10000}\u{10ffff}"',
         "".join(chr(c) for c in (0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff))),
        ("binary", r'"a\x00b\xff"', b"a\x00b\xff"),
    ):
        yield f"panic_{label}", f"pub fn main() void {{ @panic({literal}); }}\n", message
    literal = '"' + "".join(f"\\x{c:02x}" for c in range(256)) + '"'
    yield "panic_all_bytes", f"pub fn main() void {{ @panic({literal}); }}\n", bytes(range(256))
    yield "panic_nested", 'pub fn main() void { @panic(@panic("inner")); }\n', "inner"
    yield "panic_unreachable_argument", "pub fn main() void { @panic(unreachable); }\n", "reached unreachable code"
    yield "panic_alias", 'const text = value: { break :value "alias"; };\npub fn main() void { const local = text; @panic(local); }\n', "alias"
    for c, message in (("true", "left"), ("false", "rght")):
        source = f'fn operation(c: bool) noreturn {{ @panic(if (c) "left" else "rght"); }}\npub fn main() void {{ operation({c}); }}\n'
        yield f"panic_choice_{c}", source, message
    yield "panic_variable", 'fn operation() noreturn { var text = "first"; text = "later"; @panic(text); }\npub fn main() void { operation(); }\n', "later"
    slashes = chr(92) * 2
    source = "\n".join(("pub fn main() void { @panic(", "    " + slashes + "first line", "    " + slashes + "raw " + chr(92) + "u{41}", "); }", ""))
    yield "panic_multiline", source, "first line\nraw " + chr(92) + "u{41}"
    yield "panic_cleanup", 'fn operation(x: u8) void { var a = x; defer a += 1; defer @panic("cleanup"); }\npub fn main() void { operation(255); }\n', "cleanup"
    yield "panic_before_cleanup", 'fn operation(x: u8) void { var a = x; defer a += 1; @panic("direct"); }\npub fn main() void { operation(255); }\n', "direct"
    for label, source in (
        ("direct", "pub fn main() void { unreachable; }"),
        ("noreturn_main", "pub fn main() noreturn { unreachable; }"),
        ("noreturn_call", "fn stop() noreturn { unreachable; }\npub fn main() void { stop(); }"),
        ("runtime_branch", "fn operation(c: bool) void { if (c) unreachable; }\npub fn main() void { operation(true); }"),
        ("loop", "fn operation(n: usize) void { for (0..n) |_| { unreachable; } }\npub fn main() void { operation(1); }"),
        ("skip_cleanup", "fn operation(x: u8) void { var a = x; defer a += 1; unreachable; }\npub fn main() void { operation(255); }"),
        ("cleanup", "fn operation(x: u8) void { var a = x; defer a += 1; defer unreachable; }\npub fn main() void { operation(255); }"),
        ("cleanup_call", "fn stop() noreturn { unreachable; }\nfn operation(x: u8) void { var a = x; defer a += 1; defer stop(); }\npub fn main() void { operation(255); }"),
    ):
        yield f"unreachable_{label}", source + "\n", "reached unreachable code"
    for label, exit_stmt in (("return", "return a;"), ("fallthrough", ""), ("break", "break;"), ("continue", "continue;")):
        if label == "return":
            operation = f"fn operation(x: u8) u8 {{ var a = x; defer a += 1; {exit_stmt} }}"
        elif label == "fallthrough":
            operation = f"fn operation(x: u8) void {{ var a = x; defer a += 1; {exit_stmt} }}"
        else:
            operation = f"fn operation(x: u8) void {{ for (0..1) |_| {{ var a = x; defer a += 1; {exit_stmt} }} }}"
        source = operation + "\npub fn main() void { _ = operation(255); }\n"
        yield f"defer_{label}_overflow", source, "integer overflow"
    source = ("fn operation(x: u8, n: u8, d: u8) void { var a = x; defer a += 1; defer _ = @divTrunc(n, d); }\n"
              "pub fn main() void { operation(255, 3, 0); }\n")
    yield "defer_panic_order", source, "division by zero"
    source = ("fn operation(x: u8, n: u8, d: u8) u8 { var a = x; defer a += 1; return @divTrunc(n, d); }\n"
              "pub fn main() void { _ = operation(255, 3, 0); }\n")
    yield "defer_return_operand_panic", source, "division by zero"
    source = ("fn operation(start: usize, end: usize) void { for (start..end) |_| {} }\n"
              "pub fn main() void { operation(2, 1); }\n")
    yield "for_reversed", source, "integer overflow"
    source = ("fn operation(n: usize) void { for (0..n, 3..5) |_, _| {} }\n"
              "pub fn main() void { operation(1); }\n")
    yield "for_known_mismatch", source, "for loop over objects with non-equal lengths"
    for n, m, label in ((1, 2, "runtime"), (0, 1, "zero")):
        source = ("fn operation(n: usize, m: usize) void { for (0..n, 0..m) |_, _| {} }\n"
                  f"pub fn main() void {{ operation({n}, {m}); }}\n")
        yield f"for_{label}_mismatch", source, "for loop over objects with non-equal lengths"
    source = ("fn operation(start: usize) void { for (0..2, start..) |_, i| { _ = i; } }\n"
              "pub fn main() void { operation(0xffffffffffffffff); }\n")
    yield "for_counter_overflow", source, "integer overflow"
    source = ("fn operation(c: bool, a: u8) u8 { return value: { if (c) break :value a + 1; break :value 0; }; }\n"
              "pub fn main() void { _ = operation(true, 255); }\n")
    yield "block_break_overflow", source, "integer overflow"
    source = ("fn operation(c: bool, a: u16) u8 { return value: { if (c) break :value @intCast(a); break :value 0; }; }\n"
              "pub fn main() void { _ = operation(true, 256); }\n")
    yield "block_break_cast", source, "integer does not fit in destination type"
    source = ("fn operation(a: u8) u8 { var x = a; while (true) { x += 1; if (x == 0) break; } return x; }\n"
              "pub fn main() void { _ = operation(255); }\n")
    yield "loop_body_overflow", source, "integer overflow"
    source = ("fn operation(a: u8) u8 { var x = a; var i: u8 = 0; while (i < 2) : (x += 1) { i += 1; } return x; }\n"
              "pub fn main() void { _ = operation(255); }\n")
    yield "loop_continue_overflow", source, "integer overflow"
    for bits in (1, 5, 8, 32, 33, 64):
        unsigned = f"u{bits}"
        signed = f"i{bits}"
        maximum = (1 << bits) - 1
        minimum = -(1 << (bits - 1))
        signed_max = (1 << (bits - 1)) - 1
        yield binary_case(f"add_{unsigned}", unsigned, "+", maximum, 1)
        yield binary_case(f"sub_{unsigned}", unsigned, "-", 0, 1)
        if bits > 1:
            yield binary_case(f"mul_{unsigned}", unsigned, "*", maximum, 2)
            yield binary_case(f"add_positive_{signed}", signed, "+", signed_max, 1)
            yield binary_case(f"sub_negative_{signed}", signed, "-", minimum, 1)
            yield binary_case(f"mul_positive_{signed}", signed, "*", signed_max, 2)
            yield binary_case(f"mul_below_min_{signed}", signed, "*", minimum, 2)
        yield binary_case(f"add_negative_{signed}", signed, "+", minimum, -1)
        yield binary_case(f"sub_positive_{signed}", signed, "-", signed_max, -1)
        yield binary_case(f"mul_negative_{signed}", signed, "*", minimum, -1)
        source = (f"fn operation(a: {signed}) {signed} {{ return -a; }}\n"
                  f"pub fn main() void {{ _ = operation({minimum}); }}\n")
        yield f"negate_{signed}", source, "integer overflow"
    for name, from_type, to_type, value in (
        ("cast_negative", "i64", "u64", -1),
        ("cast_unsigned_narrow", "u64", "u8", 256),
        ("cast_signed_low", "i64", "i8", -129),
        ("cast_signed_high", "i64", "i8", 128),
        ("cast_sign_change", "u64", "i64", 1 << 63),
        ("cast_small_sign_change", "u8", "i8", 128),
        ("cast_unsigned_zero", "u8", "u0", 1),
        ("cast_signed_zero", "i1", "u0", -1),
    ):
        source = (f"fn operation(a: {from_type}) {to_type} {{ return @intCast(a); }}\n"
                  f"pub fn main() void {{ _ = operation({value}); }}\n")
        yield name, source, "integer does not fit in destination type"
    for integer_type in ("u32", "u64"):
        for op, label in (("/", "div"), ("%", "rem")):
            yield binary_case(f"{label}_zero_{integer_type}", integer_type, op, 123, 0, "division by zero")

    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact", "rem", "mod"):
        for integer_type in ("i8", "i64", "u64"):
            source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @{builtin}(a, b); }}\n"
                      "pub fn main() void { _ = operation(3, 0); }\n")
            yield f"{builtin}_zero_{integer_type}", source, "division by zero"
    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact"):
        for bits in (1, 8, 33, 64):
            source = (f"fn operation(a: i{bits}, b: i{bits}) i{bits} {{ return @{builtin}(a, b); }}\n"
                      f"pub fn main() void {{ _ = operation({-(1 << (bits - 1))}, -1); }}\n")
            yield f"{builtin}_overflow_i{bits}", source, "integer overflow"
    for integer_type in ("u8", "u64", "i8", "i64"):
        source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @divExact(a, b); }}\n"
                  "pub fn main() void { _ = operation(5, 2); }\n")
        yield f"exact_remainder_{integer_type}", source, "exact division produced remainder"
    for bits in (5, 33):
        for op, label in (("<<", "shl"), (">>", "shr")):
            source = (f"fn operation(a: u{bits}, b: u{(bits - 1).bit_length()}) u{bits} {{\n"
                      f"    return a {op} b;\n}}\n"
                      f"pub fn main() void {{ _ = operation(1, {bits}); }}\n")
            yield f"{label}_size_{bits}", source, "shift amount is greater than the type size"


def selected_cases(shard_index, shard_count, names=None):
    if shard_count < 1 or shard_index < 0 or shard_index >= shard_count:
        raise ValueError('invalid safety shard')
    available = list(cases())
    requested = set(names) if names is not None else None
    if requested is not None:
        unknown = requested - {case[0] for case in available}
        if unknown:
            raise ValueError('unknown safety case: ' + ', '.join(sorted(unknown)))
    selected = [case for index, case in enumerate(available)
                if index % shard_count == shard_index
                and (requested is None or case[0] in requested)]
    if requested is not None and not selected:
        raise ValueError('no requested safety cases belong to this shard')
    return iter(selected)


# Bound bytes before repr: escaping may expand output, but never without a bound.
# repr preserves binary/terminal-control evidence without emitting it literally.
def diagnostic_bytes(data, limit=2048):
    data = data or b''
    shown = repr(data[:limit])
    if len(data) > limit:
        shown += f' [truncated: {len(data) - limit} of {len(data)} bytes omitted]'
    return shown


def report_failure(name, compiler, phase, command, elapsed, error, result):
    if isinstance(error, subprocess.TimeoutExpired):
        stdout, stderr = error.output, error.stderr
    else:
        stdout, stderr = result.stdout, result.stderr
    reason = str(error).encode('utf-8', errors='backslashreplace')
    argv = repr(command).encode('utf-8', errors='backslashreplace')
    print(f'FAIL {name}: {type(error).__name__}: {diagnostic_bytes(reason)}', flush=True)
    print(f'  phase={phase} elapsed={elapsed:.6f}s '
          f'returncode={result.returncode if result is not None else "unavailable"} '
          f'compiler={diagnostic_bytes(os.fsencode(compiler))} '
          f'argv={diagnostic_bytes(argv)}', flush=True)
    print(f'  stdout={diagnostic_bytes(stdout)}', flush=True)
    print(f'  stderr={diagnostic_bytes(stderr)}', flush=True)


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('szc', nargs='?', default='build/asan/szc')
    parser.add_argument('--shard-index', type=int, default=0)
    parser.add_argument('--shard-count', type=int, default=1)
    parser.add_argument('--case', dest='case_names', action='append',
                        help='select a named case (repeatable); shard membership stays unchanged')
    args = parser.parse_args(argv)
    if args.shard_count < 1 or not 0 <= args.shard_index < args.shard_count:
        parser.error('shard count must be positive and 0 <= shard index < shard count')
    try:
        args.selected = list(selected_cases(args.shard_index, args.shard_count, args.case_names))
    except ValueError as error:
        parser.error(str(error))
    return args


def main():
    args = arguments()
    szc = args.szc
    zig = os.environ.get("ZIG")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="szc-safety-") as directory:
        root = Path(directory)
        for case in args.selected:
            name, source, message = case[:3]
            reference_message = case[3] if len(case) == 4 else message
            src = root / f"{name}.zig"
            src.write_text(source)
            try:
                message_bytes = message.encode() if isinstance(message, str) else message
                for compiler in ([szc, zig] if zig else [szc]):
                    if os.environ.get("GITHUB_ACTIONS") == "true":
                        print(f"checking safety {name}: {compiler}", flush=True)
                    exe = root / ("reference" if compiler == zig else "native")
                    command = ([compiler, "build-exe", "-j2", str(src), f"-femit-bin={exe}"]
                               if compiler == zig else
                               [compiler, "build-exe", str(src), "-o", str(exe)])
                    phase = 'compile'
                    result = None
                    started = time.monotonic()
                    compiled = run(command, timeout=120)
                    result = compiled
                    elapsed = time.monotonic() - started
                    if compiled.returncode != 0:
                        raise AssertionError("compilation failed: " + compiled.stderr.decode(errors="replace"))
                    phase = 'native'
                    command = [str(exe)]
                    result = None
                    started = time.monotonic()
                    result = run(command)
                    elapsed = time.monotonic() - started
                    if result.returncode != -signal.SIGABRT:
                        raise AssertionError(f"{compiler} exit {result.returncode}; expected SIGABRT")
                    if result.stdout:
                        raise AssertionError("unexpected stdout")
                    if compiler == zig:
                        reference_bytes = reference_message.encode() if isinstance(reference_message, str) else reference_message
                        if reference_bytes not in result.stderr:
                            raise AssertionError("reference panic message mismatch: " + result.stderr.decode(errors="replace"))
                    elif result.stderr != message_bytes + b"\n":
                        raise AssertionError("panic message mismatch: " + result.stderr.decode(errors="replace"))
                passed += 1
            except (AssertionError, subprocess.TimeoutExpired) as error:
                if isinstance(error, subprocess.TimeoutExpired):
                    elapsed = time.monotonic() - started
                report_failure(name, compiler, phase, command, elapsed, error, result)
                failed += 1
    print(f"safety: {passed} passed, {failed} failed", flush=True)
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
