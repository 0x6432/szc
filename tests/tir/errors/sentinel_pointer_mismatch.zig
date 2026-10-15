fn f(p: [*:1]const u8) [*:0]const u8 { return p; }
pub fn main() void { _ = f(undefined); }
