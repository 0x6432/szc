var a: [2]u8 = .{7,9};
const s: []u8 = &a;
pub fn main() u8 { const owned = (&s.*).*; const p = &s.*; p[0] = 3; return owned[0]; }
