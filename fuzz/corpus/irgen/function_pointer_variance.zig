// exit: 0
var value: u8 = 17;
fn read(p: *const u8) u8 { return p.*; }
fn write(p: *u8) *u8 { p.* += 1; return p; }
fn mutable() *u8 { return &value; }
fn invoke(p: *const fn (*u8) u8) u8 { return p(&value); }
fn return_const(p: *const fn () *const u8) u8 { return p().*; }
fn select(c: bool) *const fn (*u8) u8 { return if (c) read else &read; }
fn one(c: bool, p: *const fn (*u8) *const u8) u8 {
    const chosen = if (c) p else &write;
    return chosen(&value).*;
}
fn same(n: usize) u64 { return n; }
pub fn main() u8 {
    if (invoke(&read) != 17 or select(false)(&value) != 17) return 1;
    if (return_const(&mutable) != 17) return 2;
    if (one(true, &write) != 18 or one(false, &write) != 19) return 3;
    const alias: *const fn (u64) usize = &same;
    if (alias(41) != 41) return 4;
    const body: fn (*u8) u8 = read;
    if (body(&value) != 19) return 5;
    return 0;
}
