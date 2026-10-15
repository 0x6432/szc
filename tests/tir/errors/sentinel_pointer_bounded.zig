fn f(p: [*:0]const u8) [*:0]const u8 { return p[0..2]; }
pub fn main() void { _ = f(undefined); }
