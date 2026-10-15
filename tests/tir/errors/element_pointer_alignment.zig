const a: [2]u8 align(8) = .{1,2};
const p: *align(8) const u8 = &a[1];
pub fn main() void { _ = p; }
