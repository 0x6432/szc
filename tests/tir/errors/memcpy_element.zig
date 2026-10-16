pub fn main() void { var a:[1]u8=undefined; const b:[1]i8=.{1}; @memcpy(&a,&b); }
