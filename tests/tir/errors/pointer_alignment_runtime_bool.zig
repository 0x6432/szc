fn f(n: bool) void { const P = *align(@intFromBool(n)) u8; _ = P; } pub fn main() void { f(true); }
