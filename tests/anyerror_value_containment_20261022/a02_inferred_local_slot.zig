export fn f() void { var x = @as(anyerror, undefined); _ = &x; }
pub fn main() void {}
