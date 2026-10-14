fn target() u8 { return 7; } const value align(64) = target;
pub fn main() void { _ = &value; }
