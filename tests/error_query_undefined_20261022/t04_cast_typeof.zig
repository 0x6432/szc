comptime { const x: error{A,B} = undefined; _ = @TypeOf(@as(error{A}, @errorCast(x))); }
pub fn main() void {}
