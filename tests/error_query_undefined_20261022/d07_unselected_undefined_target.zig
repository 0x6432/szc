comptime { const T: type = undefined; const y = if (true) 1 else @as(T, @errorCast(undefined)); _ = y; }
pub fn main() void {}
