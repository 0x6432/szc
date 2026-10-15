fn f(s: []const u8) bool { return s != s; } pub fn main() void { _ = f("hi"); }
