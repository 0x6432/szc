fn f(p: [*]const u8, q: [*]const u16) usize { return p-q; } pub fn main() void { _ = f(undefined,undefined); }
