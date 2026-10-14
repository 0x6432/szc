fn target() align(@panic("alignment")) u8 { return 7; }
pub fn main() u8 { return target(); }
