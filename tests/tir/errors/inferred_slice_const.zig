fn f(c: bool, a: *[2]u8, b: *const [3]u8) void { const s = if (c) a else b; s[0] = 1; } pub fn main() void { f(true,undefined,undefined); }
