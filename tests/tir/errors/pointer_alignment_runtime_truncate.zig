fn f(n: u64) void { const P = *align(@truncate(n)) u8; _ = P; } pub fn main() void { f(4); }
