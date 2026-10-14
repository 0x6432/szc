fn f(p: *const u8) void { p.* = 1; }
pub fn main() void { var n: u8 = 0; f(&n); }
