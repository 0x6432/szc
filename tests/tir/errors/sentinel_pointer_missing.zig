fn f(p: [*]const u8) [*:0]const u8 { return p; }
pub fn main() void { _ = f(undefined); }
