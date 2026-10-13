fn f(c: bool) u8 { const x = if (c) 1 else 1; return x; }
pub fn main() u8 { return f(false); }
