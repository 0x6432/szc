fn f(s: []u8,n: usize) *[0]u8 { return s[n..n]; } pub fn main() void { _ = f(undefined,1); }
