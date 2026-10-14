const holder: *align(64) const u8 = &value; const value: u8 align(64) = true;
pub fn main() void { _ = holder; }
