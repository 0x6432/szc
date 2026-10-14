fn target() u8 { return 7; }
fn first(c: bool, p: *align(16) fn () u8) u8 { const f = if (c) target else p; return f(); }
fn second(c: bool, p: *align(16) fn () u8) u8 { const f = if (c) p else target; return f(); }
pub fn main() void { _ = first(true, undefined); _ = second(false, undefined); }
