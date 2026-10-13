fn f(c: bool) u8 { const T = value: { if (c) break :value u8; break :value i8; }; const x: T = 1; return @intCast(x); }
pub fn main() u8 { return f(false); }
