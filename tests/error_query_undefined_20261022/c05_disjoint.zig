comptime { const x: error{B} = undefined; const y: error{A} = @errorCast(x); _ = y; }
pub fn main() void {}
