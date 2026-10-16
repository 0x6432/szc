fn f(p: *u8) *align(16) u8 { return @alignCast((@constCast(@alignCast(p)))); } pub fn main() void { _ = f(undefined); }
