const a: [2][18446744073709551615]u16 = undefined;
const p = &a[1];
pub fn main() void { _ = p; }
