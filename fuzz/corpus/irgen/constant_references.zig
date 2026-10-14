// exit: 0
const global: u16 = 519;
fn read_nested(p: *const *const u8) u8 { return p.*.*; }
fn read(p: *const u16) u16 { return p.*; }
fn choose(n: u16, c: bool) bool {
    const alias = n;
    const first = if (c) &alias else &n;
    const second = if (!c) &alias else &n;
    return first == second and first.* == n;
}
fn nested(p: *const *const u16) u16 { return p.*.*; }
fn capture(n: u16, c: bool) u16 {
    const alias = n;
    const ptr = if (c) &alias else &alias;
    return nested(&ptr);
}
fn temporary(n: u16) u16 { return read(&(n + 1)); }
pub fn main() u8 {
    if (!choose(7, true) or !choose(9, false)) return 1;
    if (capture(29, true) != 29 or capture(31, false) != 31) return 2;
    if (temporary(41) != 42 or read(&global) != 519) return 3;
    if (read(&@as(u16, 43)) != 43 or read(&44) != 44) return 4;
    const n: u16 = 47;
    const a = &n;
    const b = &a;
    if (read(b.*) != 47 or a != &n) return 5;
    const byte: u8 = 49;
    const ptr = &byte;
    if (read_nested(&ptr) != 49) return 6;
    const T = (&u8).*;
    const typed: T = 0;
    return typed;
}
