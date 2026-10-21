export fn f() void { if (false) { var x: anyerror = undefined; _ = &x; } }
pub fn main() void {}
