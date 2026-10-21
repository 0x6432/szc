export fn f(p: *const fn (anyerror) void) void { p(undefined); }
pub fn main() void {}
