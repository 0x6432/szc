fn f() u8 { return 7; }
pub fn main() void { const value align(64) = f; _ = &value; }
