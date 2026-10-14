fn target(p: *u8) void { _ = p; } fn take(f: *const fn (*volatile u8) void) void { _ = f; } pub fn main() void { take(target); }
