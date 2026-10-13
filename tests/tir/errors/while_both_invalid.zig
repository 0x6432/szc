fn f(c: bool) u8 { return while (c) { break 256; } else 257; }
pub fn main() u8 { return f(false); }
