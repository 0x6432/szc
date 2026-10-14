fn f(p: *align(1) u32) *u32 { return p; } pub fn main() void { _ = f(undefined); }
