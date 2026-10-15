var a: [4]u16 = .{2,3,5,7};
fn window(p: []u16, lo: usize, hi: usize) []u16 { return p[lo..hi]; }
pub fn main() u8 { const s = window(&a,1,3); return @intCast(s[0]); }
