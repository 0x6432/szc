fn convert(p: *const volatile u8) *const u8 { return p; } pub fn main() void { _ = convert(undefined); }
