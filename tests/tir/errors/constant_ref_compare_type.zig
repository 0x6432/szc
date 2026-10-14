fn f(a: *const u8, b: *const u16) bool { return a == b; }
pub fn main() void { const a: u8 = 1; const b: u16 = 2; _ = f(&a, &b); }
