comptime { const x: error{A} = undefined; const y: error{} = @errorCast(x); _ = y; }
pub fn main() void {}
