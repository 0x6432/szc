fn f(p: **u32) **align(1) u32 { return p; } pub fn main() void { _ = f(undefined); }
