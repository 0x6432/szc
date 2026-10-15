fn f(c: bool, a: *[2]u8, b: *[3]u16) void { const s = if (c) a else b; _ = s; } pub fn main() void { f(true,undefined,undefined); }
