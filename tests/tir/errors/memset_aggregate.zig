fn f(p:[][2]u8) void { @memset(p,.{1,2}); }
pub fn main() void { var a:[1][2]u8=.{.{0,0}}; f(&a); }
