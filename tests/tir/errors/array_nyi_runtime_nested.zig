fn read(p: *const [2][2]u8, n: usize) [2]u8 { return p[n]; }
pub fn main() void { _ = read(undefined, 0); }
