pub fn main() void { const value: u8 align(@panic("local alignment panic")) = 1; _ = &value; }
