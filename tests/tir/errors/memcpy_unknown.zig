fn f(d:[*]u8,s:[*]const u8) void { @memcpy(d,s); } pub fn main() void { var a:[1]u8=.{0}; f(&a,"a"); }
