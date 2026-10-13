fn f(c: bool) u8 { const x = if (c) @as(u8, 1) else false; return x; }
pub fn main() u8 { return f(false); }
