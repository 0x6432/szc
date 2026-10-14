fn f(p: **u8) **allowzero u8 { return p; }
pub fn main() void { _ = f(undefined); }
