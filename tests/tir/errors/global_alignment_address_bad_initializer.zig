var holder: *align(64) u8 = &value; var value: u8 align(64) = true;
pub fn main() void { _ = holder; }
