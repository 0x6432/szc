fn f(s: []const u8) [:0]const u8 { return s; } pub fn main() void { _ = f(undefined); }
