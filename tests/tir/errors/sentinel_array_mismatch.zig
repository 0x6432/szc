fn f(a: [2:1]u8) [2:0]u8 { return a; }
pub fn main() void { _ = f(undefined); }
