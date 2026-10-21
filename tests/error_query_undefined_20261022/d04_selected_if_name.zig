comptime { const y = if (false) "ok" else @errorName(@as(error{A}, undefined)); _ = y; }
pub fn main() void {}
