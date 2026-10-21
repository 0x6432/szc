comptime { const x: error{} = undefined; _ = @errorName(x); }
pub fn main() void {}
