comptime { const y = if (true) "ok" else @errorName(@as(error{A}, undefined)); _ = y; }
pub fn main() void {}
