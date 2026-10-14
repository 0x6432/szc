fn f(n: u8) void { var value: u8 align(n) = 1; value += 0; } pub fn main() void { f(4); }
