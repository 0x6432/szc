const a: *const [2]u8 = "ab";
const p = &a[2];
pub fn main() void { _ = p; }
