fn get(p: [*]const u16, i: usize) u16 { return p[i]; }
fn put(p: [*]u16, i: usize, n: u16) void { p[i] = n; }
pub fn main() u8 { var a = [2]u16{7,9}; put(&a,1,13); return @intCast(get(&a,1)); }
