fn convert(p: **u8) **volatile u8 { return p; } pub fn main() void { _ = convert(undefined); }
