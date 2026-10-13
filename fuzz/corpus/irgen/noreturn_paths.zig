fn stop() noreturn { unreachable; }
fn choose(c: bool, x: u8) u8 { return if (c) x else stop(); }
fn cleanup(c: bool) u8 { if (c) { defer stop(); return 1; } return 2; }
pub fn main() u8 { return choose(true, 7) + cleanup(false); }
