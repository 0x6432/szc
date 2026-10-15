const a = [_]u8{1,2};
const p = &a[1];
pub fn main() void { p.* = 3; }
