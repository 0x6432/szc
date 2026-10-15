const a = [_]u8{1,2};
const p = &a[undefined];
pub fn main() void { _ = p; }
