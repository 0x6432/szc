fn read(p: *const [9223372036854775808]u16, n: usize) u16 { return p[n]; }
pub fn main() void { _ = read(undefined, 0); }
