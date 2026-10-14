var n: u16 = 7;
var p = &n;
pub fn main() u8 { p.* += 2; return @intCast(n); }
