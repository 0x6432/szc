fn f(c: bool) u8 { return value: { if (c) break :value 256; break :value 257; }; }
pub fn main() u8 { return f(false); }
