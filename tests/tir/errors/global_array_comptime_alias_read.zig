var a: [2]u8 = .{1,2};
const p: *const u8 = &a[0];
const n = p.*;
pub fn main() void { _ = n; }
