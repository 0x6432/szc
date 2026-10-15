fn read(p: *const [0]u8, n: usize) u8 { return p[n]; }
pub fn main() void { _ = read(undefined, 0); }
