fn f(p: *[3:17]u8) *[3]u8 { return @alignCast(p); } pub fn main() void { _ = f(undefined); }
