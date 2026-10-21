comptime { const T: type = undefined; _ = @as(T, @errorCast(123)); }
pub fn main() void {}
