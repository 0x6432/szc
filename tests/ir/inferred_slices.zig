fn f(c: bool, a: *[2]u8, b: *[3]u8) u8 { const s = if (c) a else b; return s[0]; }
pub fn main() u8 { var a = [2]u8{3,5}; var b = [3]u8{7,9,11}; return f(true,&a,&b); }
