comptime { const y: error{A} = if (true) error.A else @errorCast(@as(error{B}, undefined)); _ = @errorName(y); }
pub fn main() void {}
