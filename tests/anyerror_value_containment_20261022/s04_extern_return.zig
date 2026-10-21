extern fn produce() anyerror; export fn f() void { const x = produce(); _ = @TypeOf(x); }
pub fn main() void {}
