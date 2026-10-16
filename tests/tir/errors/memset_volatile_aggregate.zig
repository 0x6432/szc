fn f(p:[]volatile [2]u8) void { @memset(p,.{1,2}); }
pub fn main() void { var a:[1][2]u8=undefined; f(&a); }
