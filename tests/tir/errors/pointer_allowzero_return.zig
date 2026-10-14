fn f(p: *const fn () *allowzero u8) *const fn () *u8 { return p; }
pub fn main() void { _ = f(undefined); }
