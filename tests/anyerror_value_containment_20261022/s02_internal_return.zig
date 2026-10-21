fn produce() anyerror { return undefined; } export fn f() void { const x = produce(); _ = @TypeOf(x); }
pub fn main() void {}
