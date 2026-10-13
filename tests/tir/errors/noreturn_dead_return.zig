fn stop(c: bool) noreturn { if (c) unreachable; return; }
pub fn main() void { stop(true); }
