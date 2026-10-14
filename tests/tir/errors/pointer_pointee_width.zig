fn f(p: *u8) *const u16 { return p; }
pub fn main() void { var n: u8 = 0; _ = f(&n); }
