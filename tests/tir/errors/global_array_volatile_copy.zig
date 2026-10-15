var a: [2]u8 = .{1,2};
const p: *volatile [2]u8 = &a;
pub fn main() void { const b = p.*; _ = b; }
