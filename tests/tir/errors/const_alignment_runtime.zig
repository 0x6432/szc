fn f(n: u29) void { const value: u8 align(n) = 1; _ = &value; } pub fn main() void { f(4); }
