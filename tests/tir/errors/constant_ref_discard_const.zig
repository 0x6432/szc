fn f(p: *u8) void { p.* = 1; }
pub fn main() void { const n: u8 = 0; f(&n); }
