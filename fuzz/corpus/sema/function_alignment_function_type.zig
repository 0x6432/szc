const P = *const fn () align(16) u8; fn target() u8 { const p: P = undefined; _ = p; return 7; }
pub fn main() u8 { return target(); }
