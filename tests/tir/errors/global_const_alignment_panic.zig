const value: u8 align(@panic("global constant alignment panic")) = 1;
pub fn main() void { _ = &value; }
