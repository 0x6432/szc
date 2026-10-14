fn f(p: *const fn (*u8) void) *const fn (*allowzero u8) void { return p; }
pub fn main() void { _ = f(undefined); }
