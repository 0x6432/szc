var n: u8 = 1;
const p: *const u8 = &n;
pub fn main() void { const q: *u8 = p; _ = q; }
