comptime { const x: error{A,B} = undefined; _ = @errorName(x); }
pub fn main() void {}
