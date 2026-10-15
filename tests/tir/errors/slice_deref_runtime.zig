fn f(s: []u8) void { _ = s.*; } pub fn main() void { f(undefined); }
