fn f(p: *const volatile u8) void { _ = @volatileCast((@constCast(@volatileCast(p)))); }
pub fn main() void { f(undefined); }
