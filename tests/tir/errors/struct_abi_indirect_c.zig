const S = struct { a: u8 };
pub fn main() void {
	const p: *const fn (S) callconv(.c) void = @ptrFromInt(4096);
	p(.{ .a = 1 });
}
