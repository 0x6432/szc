pub fn main() void { const value: u8 = 7; const p: *const volatile u8 = &value; p.* = 9; }
