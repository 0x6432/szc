fn f(p: *align(32) []u8) *align(32) usize { return &p.len; } pub fn main() void { _ = f(undefined); }
