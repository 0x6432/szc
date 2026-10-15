const a = [2]u8{7,9}; const p: [*]const u8 = &a; const n = p - (p+1); pub fn main() void { _ = n; }
