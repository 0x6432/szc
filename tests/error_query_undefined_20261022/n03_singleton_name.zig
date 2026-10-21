comptime { const x: error{A} = undefined; _ = @errorName(x); }
pub fn main() void {}
