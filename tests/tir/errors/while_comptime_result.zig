fn f(c: bool) u8 { const x = while (c) { break 1; } else 2; return x; }
pub fn main() u8 { return f(false); }
