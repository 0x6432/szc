comptime { const x: error{} = undefined; const y: error{A} = @errorCast(x); _ = y; }
pub fn main() void {}
