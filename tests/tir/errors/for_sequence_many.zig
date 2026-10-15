fn f(p: [*]u8) void { for (p) |_| {} } pub fn main() void { f(undefined); }
