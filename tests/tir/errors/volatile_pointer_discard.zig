fn convert(p: *volatile u8) *u8 { return p; } pub fn main() void { _ = convert(undefined); }
