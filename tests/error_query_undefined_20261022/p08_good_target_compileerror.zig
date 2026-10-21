comptime { _ = @as(error{A}, @errorCast(@compileError("operand demanded"))); }
pub fn main() void {}
