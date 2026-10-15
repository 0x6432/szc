const a: [2][18446744073709551615]type = undefined;
const p = &a[1];
const q = &p[1];
pub fn main() void { _ = q; }
