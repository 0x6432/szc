var a: [2]u8 = .{1,2};
var b: [2]u8 = a;
pub fn main() void { _ = &b; }
