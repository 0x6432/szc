fn f(n: u29) void { const P = *align(~n) u8; _ = P; } pub fn main() void { f(4); }
