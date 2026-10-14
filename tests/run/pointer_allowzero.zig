// exit: 0
var count: u8 = 7;
var global_pointer: *allowzero u8 = &count;
const fixed_pointer: *allowzero const u8 = &count;
const pointer_address = &fixed_pointer;
fn read(p: *allowzero const u8) u8 { return p.*; }
fn update(p: *allowzero u8, value: u8) void { p.* = value; }
fn select(c: bool, a: *const u8, b: *allowzero u8) *allowzero const u8 {
    const p = if (c) a else b;
    return p;
}
fn select_reverse(c: bool, a: *allowzero u8, b: *const u8) *allowzero const u8 {
    const p = if (c) a else b;
    return p;
}
fn nested(p: *const *allowzero const u8) u8 { return p.*.*; }
fn accepts_zero(p: *allowzero u8) u8 { return p.*; }
fn returns_nonzero() *u8 { return &count; }
fn call(p: *allowzero const fn (*u8) u8, arg: *u8) u8 { return p(arg); }
fn zero_size(p: *allowzero u0, q: *allowzero void) void {
    p.* = 0;
    q.* = {};
}
pub fn main() u8 {
    var value: u8 = 19;
    var p: *allowzero u8 = &value;
    update(p, 23);
    if (read(p) != 23) return 1;
    p = global_pointer;
    update(p, 29);
    if (count != 29 or read(fixed_pointer) != 29 or pointer_address.*.* != 29) return 2;
    if (select(true, &value, global_pointer).* != 23) return 3;
    if (select(false, &value, global_pointer).* != 29) return 4;
    if (select_reverse(true, global_pointer, &value).* != 29) return 5;
    if (select_reverse(false, global_pointer, &value).* != 23) return 6;
    const nonzero: *u8 = &value;
    if (nested(&nonzero) != 23) return 7;
    const with_zero: *allowzero u8 = &value;
    if (nested(&with_zero) != 23) return 8;
    if (call(accepts_zero, &value) != 23) return 9;
    const callback: *allowzero const fn () *allowzero u8 = &returns_nonzero;
    if (callback().* != 29) return 10;
    const known: *allowzero const u8 = &@as(u8, 31);
    if (known.* != 31 or known != &@as(u8, 31)) return 11;
    if (global_pointer != &count) return 12;
    zero_size(undefined, undefined);
    global_pointer = &value;
    if (read(global_pointer) != 23) return 13;
    if (*u8 == *allowzero u8) return 14;
    if (*allowzero const u8 != *const allowzero u8) return 15;
    if (*allowzero u8 == *allowzero const u8) return 16;
    return 0;
}
