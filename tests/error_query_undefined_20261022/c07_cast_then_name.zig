comptime { const x: error{A,B} = undefined; const y: error{A} = @errorCast(x); _ = @errorName(y); }
pub fn main() void {}
