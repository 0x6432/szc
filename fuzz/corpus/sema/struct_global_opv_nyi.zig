const Z = struct { x: u0 };
var a: Z = .{ .x = 0 };
var b: Z = a;
pub fn main() void { _ = &b; }
