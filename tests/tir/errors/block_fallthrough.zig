fn f(c: bool) u8 { return value: { if (c) break :value 1; }; }
pub fn main() u8 { return f(false); }
