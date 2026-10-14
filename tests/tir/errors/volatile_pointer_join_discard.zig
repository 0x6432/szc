fn f(flag: bool, p: *u8, q: *volatile u8) *u8 { return if (flag) p else q; } pub fn main() void { _ = f(true, undefined, undefined); }
