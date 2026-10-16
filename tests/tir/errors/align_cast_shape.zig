fn f(p: *[3]u8) [*]u8 { return @alignCast(p); } pub fn main() void { _ = f(undefined); }
