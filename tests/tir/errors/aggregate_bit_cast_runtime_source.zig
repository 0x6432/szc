fn f(n:[2]u64) u128 { return @bitCast(n); } pub fn main() void { _=f(.{1,2}); }
