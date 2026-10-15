fn f(p: **[2:0]u8) **[2]u8 { return p; }
pub fn main() void { _ = f(undefined); }
