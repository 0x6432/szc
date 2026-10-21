comptime { const x: error{A,B} = undefined; const y: error{A} = @errorCast(x); _ = y; }
pub fn main() void {}
