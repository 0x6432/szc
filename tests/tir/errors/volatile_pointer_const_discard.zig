fn convert(p: *const volatile u8) *volatile u8 { return p; } pub fn main() void { _ = convert(undefined); }
