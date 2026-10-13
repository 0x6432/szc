fn f(x: u8) void { while (x) |v| { _ = v; } }
pub fn main() void { f(1); }
