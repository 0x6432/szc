fn copy(d:[]volatile [1]u8,s:[]const [1]u8) void { @memcpy(d,s); }
pub fn main() void { var a:[1][1]u8=undefined; const b:[1][1]u8=.{.{7}}; copy(&a,&b); }
