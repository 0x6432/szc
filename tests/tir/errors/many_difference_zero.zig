const a = [2]u0{0,0}; const p: [*]const u0 = &a; const n = p - p; pub fn main() void { _ = n; }
