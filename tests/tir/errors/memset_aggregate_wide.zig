fn f(p:[][1]u65) void { @memset(p,.{1}); }
pub fn main() void { var a:[1][1]u65=undefined; f(&a); }
