pub fn main() void { var value: u8 = 7; const p: *volatile u8 = &value; _ = p.*; }
