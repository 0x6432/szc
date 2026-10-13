fn f(c: bool) u8 { const x = value: { if (c) break :value @as(u8, 1); break :value false; }; return x; }
pub fn main() u8 { return f(false); }
