fn f(n:[2]u8) u16 { return @bitCast(n); } pub fn main() void { _=f(.{1,2}); }
