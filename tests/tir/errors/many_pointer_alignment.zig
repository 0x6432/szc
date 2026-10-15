fn f(p: [*]align(1) u16) void { const q: [*]u16 = p; _ = q; } pub fn main() void { f(undefined); }
