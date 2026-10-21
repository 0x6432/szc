comptime { const T: type = undefined; _ = @as(T, @errorCast(@compileError("operand demanded"))); }
pub fn main() void {}
