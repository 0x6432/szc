fn f(x: u8) u8 { return if (x) |v| v else 1; }
pub fn main() u8 { return f(1); }
