const A = *const const u8;
const B = *volatile volatile u8;
const C = *allowzero allowzero u8;
const D = [*]const volatile const allowzero volatile allowzero u8;
const E = []const allowzero const volatile volatile allowzero u8;
const F = [*c]const const volatile volatile u8;
const G = *align(4) const volatile const allowzero volatile allowzero u32;
fn mempcpy(noalias dst: *anyopaque, noalias src: *const const anyopaque, len: usize) callconv(.c) *anyopaque {
	_ = src;
	_ = len;
	return dst;
}
