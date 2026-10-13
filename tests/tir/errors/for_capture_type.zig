fn f(n: u8) void { for (0..n) |i| { const x: u8 = i; _ = x; } }
pub fn main() void { f(2); }
