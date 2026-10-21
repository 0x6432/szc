comptime { if (false) { const y: error{A} = @errorCast(@as(error{B}, undefined)); _ = y; } }
pub fn main() void {}
