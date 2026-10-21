comptime { const x: error{A,B} = undefined; _ = @TypeOf(x); }
pub fn main() void {}
