comptime { const T: type = undefined; _ = @as(T, @errorCast(error.A)); }
pub fn main() void {}
