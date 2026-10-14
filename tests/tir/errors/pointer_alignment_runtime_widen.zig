fn f(n: u8) void { const P = *align(n) u8; _ = P; } pub fn main() void { f(4); }
