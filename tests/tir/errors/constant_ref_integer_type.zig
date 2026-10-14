fn f(p: *const u8) u8 { return p.*; }
pub fn main() u8 { const n = 1; return f(&n); }
