fn f(p: *const fn () *align(1) u32) *const fn () *u32 { return p; } pub fn main() void { _ = f(undefined); }
