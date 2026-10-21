comptime { const T: type = undefined; _ = @as(T, @errorCast(undefined)); }
pub fn main() void {}
