fn f(c: bool) u8 { const x = while (c) { break @as(u8, 1); } else false; return x; }
pub fn main() u8 { return f(false); }
