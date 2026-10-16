fn f(s: *[:17]const u8, p: [*]const u8) void { s.ptr = p; } pub fn main() void { var s: [:17]const u8 = &.{}; f(&s,undefined); }
