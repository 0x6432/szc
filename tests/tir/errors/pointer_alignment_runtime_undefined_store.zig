fn f(p: *u8) void { const P = *align(blk: { p.* = undefined; break :blk 4; }) u8; _ = P; } pub fn main() void { var n: u8 = 1; f(&n); }
