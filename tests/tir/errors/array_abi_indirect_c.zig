pub fn main() void {
	const p: *const fn ([1]u8) callconv(.c) void = @ptrFromInt(4096);
	p(.{1});
}
