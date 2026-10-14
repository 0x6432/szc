const natural: u29 = 2 + 2;
var value: u32 = 21;
var pointer: *align(1) u32 = &value;
fn read(p: *align(1) const u32) u32 { return p.*; }
fn choose(c: bool, a: *const u32, b: *align(natural) u32) *align(4) const u32 {
    const p = if (c) a else b;
    return p;
}
fn number() u8 { return 7; }
fn function(c: bool, p: *align(16) const fn () u8) *align(1) const fn () u8 {
    const f = if (c) number else p;
    return f;
}
pub fn main() u8 {
    pointer.* += 1;
    const nested: *const *align(1) const u32 = &pointer;
    if (*u32 == *align(4) u32) return 1;
    if (read(nested.*) != 22 or choose(false, &value, &value).* != 22) return 2;
    return function(true, undefined)() - 7;
}
