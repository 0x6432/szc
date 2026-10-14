fn f(n: u8) void { const value: u8 align(n) = 1; _ = &value; } pub fn main() void { f(4); }
