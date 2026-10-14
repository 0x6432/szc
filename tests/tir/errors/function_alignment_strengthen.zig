fn target() align(1) u8 { return 7; }
pub fn main() void { const p: *align(64) const fn () u8 = &target; _ = p; }
