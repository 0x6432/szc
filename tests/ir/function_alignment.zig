const A: u29 = 16;
fn target() align(A) u8 { return 7; }
const alias = target;
fn choose(flag: bool) *align(1) const fn () u8 { return if (flag) &target else &other; }
fn other() align(1) u8 { return 9; }
pub fn main() u8 { const p: *align(16) const fn () u8 = &alias; return p() + choose(false)(); }
