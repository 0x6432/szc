fn f(c: bool) u8 { const x = value: { if (c) break :value 1; break :value 1; }; return x; }
pub fn main() u8 { return f(false); }
