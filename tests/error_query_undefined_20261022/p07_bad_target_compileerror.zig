comptime { _ = @as(u8, @errorCast(@compileError("operand demanded"))); }
pub fn main() void {}
