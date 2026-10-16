fn f(p: *const u8) *align(16) u8 { return @alignCast(p); } pub fn main() void { _ = f(undefined); }
