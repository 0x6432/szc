fn f(c: bool) u8 { const T = if (c) u8 else i8; const x: T = 1; return @intCast(x); }
pub fn main() u8 { return f(false); }
