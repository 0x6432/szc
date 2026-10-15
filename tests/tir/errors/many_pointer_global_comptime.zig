var a = [2]u8{7,9}; const p: [*]const u8 = &a; const n = p[0]; pub fn main() void { _ = n; }
