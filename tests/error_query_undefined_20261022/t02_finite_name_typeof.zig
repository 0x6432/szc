comptime { const x: error{A,B} = undefined; _ = @TypeOf(@errorName(x)); }
pub fn main() void {}
