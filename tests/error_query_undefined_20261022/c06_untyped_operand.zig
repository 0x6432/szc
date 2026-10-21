comptime { const y: error{A} = @errorCast(undefined); _ = y; }
pub fn main() void {}
