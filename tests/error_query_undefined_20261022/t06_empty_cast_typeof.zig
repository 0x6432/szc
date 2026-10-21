comptime { const x: error{} = undefined; _ = @TypeOf(@as(error{A}, @errorCast(x))); }
pub fn main() void {}
