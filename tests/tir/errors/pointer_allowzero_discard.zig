fn f(p: *allowzero u8) *u8 { return p; }
pub fn main() void { _ = f(undefined); }
