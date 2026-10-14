pub fn main() void { const P = *align(@panic("alignment panic")) u8; _ = P; }
