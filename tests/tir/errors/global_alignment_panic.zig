var value: u8 align(@panic("global alignment panic")) = 1;
pub fn main() void { _ = &value; }
