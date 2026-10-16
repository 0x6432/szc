fn f(n:u16) [2]u8 { return @bitCast(n); } pub fn main() void { _=f(258); }
