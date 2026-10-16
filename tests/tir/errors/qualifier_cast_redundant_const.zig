fn f(p: *const u8) void { _ = @constCast(@constCast(p)); }
pub fn main() void { f(undefined); }
