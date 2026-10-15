const a = if (&a == &a) @as(u8, 7) else 0;
pub fn main() void { _ = &a; }
