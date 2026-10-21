comptime { const x: anyerror = undefined; _ = @TypeOf(x); }
pub fn main() void {}
