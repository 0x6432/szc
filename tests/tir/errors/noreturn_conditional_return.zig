fn stop(c: bool) noreturn { return if (c) 1 else unreachable; }
pub fn main() void { stop(false); }
