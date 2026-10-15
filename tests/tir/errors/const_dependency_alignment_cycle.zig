const a: u8 align(if (&a == &a) 64 else 1) = 7;
pub fn main() void { _ = &a; }
