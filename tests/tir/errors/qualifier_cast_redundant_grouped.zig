fn f(p: *const volatile u8) void { _ = @constCast((@volatileCast(@constCast(p)))); }
pub fn main() void { f(undefined); }
