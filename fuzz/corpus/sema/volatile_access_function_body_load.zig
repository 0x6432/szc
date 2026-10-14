fn read(p: *const volatile fn () u8) u8 { return p.*(); } pub fn main() void { _ = read(undefined); }
