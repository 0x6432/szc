fn f(p: []u8, n: u8) void { _ = p[0..2:n]; } pub fn main() void { f(undefined,17); }
