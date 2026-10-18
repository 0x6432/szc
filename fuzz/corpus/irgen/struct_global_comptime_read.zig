const S = struct { x: u8 };
var g: S = .{ .x = 1 };
const initial: S = g;
pub fn main() void { _ = initial; }
