pub fn main() void {
	const p: *const fn () callconv(.c) []const u8 = @ptrFromInt(4096);
	_ = p();
}
