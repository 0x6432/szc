comptime { const x: error{} = undefined; _ = @TypeOf(x); }
pub fn main() void {}
