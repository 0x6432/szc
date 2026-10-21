comptime { const x: error{} = undefined; _ = @TypeOf(@errorName(x)); }
pub fn main() void {}
