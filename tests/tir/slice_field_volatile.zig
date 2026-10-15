fn f(p: *volatile []u8) usize { return p.len; } pub fn main() void { _ = f(undefined); }
