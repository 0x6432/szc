comptime { const x: error{} = undefined; const y: error{} = @errorCast(x); _ = y; }
pub fn main() void {}
