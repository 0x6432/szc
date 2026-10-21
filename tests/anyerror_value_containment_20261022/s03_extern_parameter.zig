extern fn take(x: anyerror) void; export fn f() void { take(undefined); }
pub fn main() void {}
