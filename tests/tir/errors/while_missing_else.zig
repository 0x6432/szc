fn f(c: bool) u8 { return while (c) { break 1; }; }
pub fn main() u8 { return f(false); }
