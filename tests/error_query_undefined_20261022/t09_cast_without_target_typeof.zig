comptime { const x: error{A} = undefined; _ = @TypeOf(@errorCast(x)); }
pub fn main() void {}
