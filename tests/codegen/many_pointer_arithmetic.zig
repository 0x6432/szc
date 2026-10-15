fn advance(p: [*]u16, n: usize) [*]u16 { var q = p; q += n; q -= 1; return q; }
fn delta(p: [*]const u16, q: [*]const u16) usize { return p - q; }
pub fn main() u8 { var a = [3]u16{7,9,11}; const p: [*]u16 = &a; return @intCast(delta(advance(p,2),p)); }
