fn f(p: *const u8) *u8 { return p; }
pub fn main() void { var n: u8 = 0; _ = f(&n); }
