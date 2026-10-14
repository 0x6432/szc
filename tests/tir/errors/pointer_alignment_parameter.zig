fn f(p: *const fn (*u32) void) *const fn (*align(1) u32) void { return p; } pub fn main() void { _ = f(undefined); }
