fn read(p: *const [2]u8) u8 { return p[2]; }
pub fn main() void { _ = read(undefined); }
