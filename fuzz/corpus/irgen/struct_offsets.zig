const S=struct { a:u8, b:u64, c:u16, z:[0]u64, }; pub fn main() u8 { return @intCast(@offsetOf(S,"a")); }
