comptime { const x: error{B} = undefined; _ = @TypeOf(@as(error{A}, @errorCast(x))); }
pub fn main() void {}
