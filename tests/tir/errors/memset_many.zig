fn f(p:[*]u8) void { @memset(p,3); }
pub fn main() void { var a:[1]u8=.{0}; f(&a); }
