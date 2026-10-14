// exit: 0
export fn c_target(n: i32) i32 { return -n; }
var external = &c_target;
const F = fn (u8) u8;
fn add(n: u8) u8 { return n + 1; }
fn sub(n: u8) u8 { return n - 1; }
var selected: *const F = &add;
const fixed = &sub;
var nested = &fixed;
fn invoke(p: *const F, n: u8) u8 { return p(n); }
fn choose(c: bool) *const F { return if (c) &add else &sub; }
fn peer(c: bool) *const F { return if (c) add else &sub; }
fn indirect(p: *const *const F, n: u8) u8 { return p.*(n); }
fn change() u8 { selected = &sub; return 13; }
fn cleanup(p: *const fn (*u8) void, value: *u8) u8 {
    defer p(value);
    return value.*;
}
fn increment(p: *u8) void { p.* += 1; }
fn no_alias(noalias p: *u8) void { p.* += 1; }
fn zero(n: u0) u0 { return n; }
fn nothing() void {}
fn generic(n: anytype) void { _ = n; }
pub fn main() u8 {
    _ = &generic;
    if (invoke(&add, 5) != 6 or invoke(sub, 5) != 4) return 1;
    if (choose(true)(9) != 10 or choose(false)(9) != 8) return 2;
    if (peer(true)(9) != 10 or peer(false)(9) != 8) return 3;
    if (selected(change()) != 14 or selected(13) != 12) return 4;
    if (indirect(nested, 9) != 8) return 5;
    var local: *const F = &add;
    local = &sub;
    if (local(9) != 8 or (&local).*(9) != 8) return 6;
    const alias = add;
    if (&alias != &add or &add == &sub or (&add).*(3) != 4) return 7;
    var value: u8 = 29;
    if (cleanup(&increment, &value) != 29 or value != 30) return 8;
    const qualified: *const fn (noalias *u8) void = &no_alias;
    qualified(&value);
    if (value != 31) return 9;
    const z: *const fn (u0) u0 = &zero;
    if (@as(u8, z(0)) != 0) return 10;
    const v: *const fn () void = &nothing;
    v();
    if (external(-7) != 7) return 11;
    return 0;
}
