var value: u29 = 4; const p: *volatile u29 = &value; const A: u29 = p.*; pub fn main() void { const P = *align(A) u8; _ = P; }
