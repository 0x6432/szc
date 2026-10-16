fn f(p: *volatile u8) void { _ = @volatileCast(@volatileCast(p)); }
pub fn main() void { f(undefined); }
