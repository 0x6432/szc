fn take(x: anyerror) void { _ = @TypeOf(x); } export fn f() void { take(undefined); }
pub fn main() void {}
