var a: [2]u8 = .{1,2};
const p: *const u8 = &a[0];
pub fn main() void { p.* = 3; }
