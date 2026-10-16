pub fn main() void { var a:[1][1]u8=undefined; const b:[1][1]u8=.{.{7}}; @memcpy(&a,&b); }
