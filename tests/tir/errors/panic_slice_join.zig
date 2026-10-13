fn fail(c: bool) void { @panic(if (c) "a" else "longer"); }
pub fn main() void { fail(true); }
