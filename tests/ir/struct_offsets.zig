const S = struct { a:u8, b:u64, c:u16, d:u32, e:u8, };
pub fn main() u8 {
	return @intCast(@offsetOf(S, "a") + @offsetOf(S, "c"));
}
