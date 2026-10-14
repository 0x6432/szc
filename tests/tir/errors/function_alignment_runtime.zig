var A: u29 = 16; fn target() align(A) u8 { return 7; }
pub fn main() u8 { return target(); }
