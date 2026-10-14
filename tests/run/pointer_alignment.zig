// exit: 0
const alignment: u29 = 2 + 2;
var value: u32 = 123;
var pointer: *align(1) u32 = &value;
const fixed: *align(2) const u32 = &@as(u32, 77);
fn read(p: *align(1) const u32) u32 { return p.*; }
fn update(p: *align(1) u32, n: u32) void { p.* = n; }
fn choose(c: bool, a: *align(2) const u32, b: *allowzero u32) *align(2) allowzero const u32 {
    const p = if (c) a else b;
    return p;
}
fn reverse(c: bool, a: *allowzero u32, b: *align(2) const u32) *align(2) allowzero const u32 {
    const p = if (c) a else b;
    return p;
}
fn nested(p: *const *align(1) const u32) u32 { return p.*.*; }
fn accepts_unaligned(p: *align(1) const u32) u32 { return p.*; }
fn callback(p: *const fn (*const u32) u32) u32 { return p(&value); }
fn result() *u32 { return &value; }
fn number() u8 { return 9; }
fn function_join(c: bool, p: *align(16) const fn () u8) u8 {
    const q = if (c) number else p;
    return q();
}
fn zero(p: *align(16) u0, q: *align(16) void) void { p.* = 0; q.* = {}; }
pub fn main() u8 {
    var n: u32 = 19;
    const natural: *align(alignment) u32 = &n;
    const weak: *align(1) u32 = natural;
    update(weak, 23);
    if (read(&n) != 23 or natural.* != 23) return 1;
    if (choose(true, fixed, &n).* != 77 or choose(false, fixed, &n).* != 23) return 2;
    if (reverse(true, &n, fixed).* != 23 or reverse(false, &n, fixed).* != 77) return 3;
    const inner: *u32 = &n;
    if (nested(&inner) != 23) return 4;
    const other: *align(1) u32 = &n;
    if (nested(&other) != 23) return 5;
    pointer = &n;
    pointer.* += 1;
    if (read(pointer) != 24) return 6;
    if (callback(&accepts_unaligned) != 123) return 7;
    const get: *align(1) const fn () *align(1) u32 = &result;
    if (get().* != 123 or function_join(true, undefined) != 9) return 8;
    if (*u32 == *align(4) u32 or *align(1) u32 == *align(4) u32) return 9;
    if (*align(alignment) u32 != *align(4) u32) return 10;
    if (*void == *align(1) void or *u0 == *align(1) u0) return 11;
    if (*const fn () void == *align(1) const fn () void) return 12;
    if (*align(268435456) u8 == *align(134217728) u8) return 13;
    zero(undefined, undefined);
    return 0;
}
