fn f(p: *u8) void { _ = @alignCast(p); } pub fn main() void { f(undefined); }
