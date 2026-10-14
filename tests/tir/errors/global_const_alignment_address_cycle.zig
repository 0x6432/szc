const value: u8 align(64) = if (&value == &value) 7 else 0;
pub fn main() void { _ = &value; }
