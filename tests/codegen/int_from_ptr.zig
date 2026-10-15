fn address(p: [*]const u8) usize { return @intFromPtr(p); }
pub fn main() u8 { const a = [2]u8{7,9}; if (address(&a) == 0) return 1; return 0; }
