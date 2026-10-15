const p: *const fn () u8 = target;
fn target() u8 { return 256; }
pub fn main() void { _ = &p; }
