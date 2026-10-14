fn f(a: *u8, b: *u8) bool { return a < b; }
pub fn main() void { var a: u8 = 1; var b: u8 = 2; _ = f(&a, &b); }
