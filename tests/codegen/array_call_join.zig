fn choose(a: [2]u16, c: bool) [2]u16 { return if (c) a else .{3, 4}; }
pub fn main() u8 { const a = choose(.{1, 2}, true); return @intCast(a[0]); }
