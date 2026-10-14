fn same(a: *u8, b: *u8) bool { return a == b; }
pub fn main() void { var n: u8 = 1; _ = same(&n, &n); }
