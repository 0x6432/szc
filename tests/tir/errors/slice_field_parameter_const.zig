fn f(s: []u8) void { s.len = 1; } pub fn main() void { f(undefined); }
