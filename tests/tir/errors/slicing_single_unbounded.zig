fn f(p: *u8) void { _ = p[0..]; } pub fn main() void { f(undefined); }
