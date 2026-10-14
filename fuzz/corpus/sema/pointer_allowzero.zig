var value: u8 = 11;
var pointer: *allowzero u8 = &value;
const saved: *allowzero const u8 = &@as(u8, 13);
fn read(p: *allowzero const u8) u8 { return p.*; }
fn select(c: bool, a: *const u8, b: *allowzero u8) *allowzero const u8 {
    const p = if (c) a else b;
    return p;
}
fn call(p: *allowzero const fn (*u8) u8) u8 { return p(&value); }
pub fn main() u8 {
    pointer.* += 1;
    const nested: *const *allowzero const u8 = &pointer;
    const known: *allowzero const u8 = &@as(u8, 13);
    if (known != saved) return 1;
    if (*allowzero u8 == *u8) return 2;
    return select(false, &value, pointer).* + read(nested.*) + call(read) - 36;
}
