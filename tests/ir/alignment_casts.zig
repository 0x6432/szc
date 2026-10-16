// exit: 0
var values: [3:17]u16 align(16) = .{7,9,11};
const text_backing: [3:0]u8 align(16) = .{97,98,99};
fn one(p: *align(1) const volatile u16) *align(16) u16 {
	return @volatileCast((@alignCast(@constCast(p))));
}
fn many(p: [*]align(1) u16) [*]align(16) u16 { return @alignCast(p); }
fn slice(p: [:17]align(1) u16) [:17]align(16) u16 { return @alignCast(p); }
fn array(p: *align(1) [3:17]u16) [:17]align(16) u16 { return @alignCast(p); }
fn empty(p: []u8) []align(16) u8 { return @alignCast(p); }
fn address(p: *align(16) const [3:0]u8) u8 { return @intCast(@intFromPtr(p) & 15); }
pub fn main() u8 {
	one(&values[0]).* = 19;
	if (values[0] != 19 or many(&values)[1] != 9) return 1;
	const s = slice(&values);
	if (s.len != 3 or s[3] != 17 or s[0] != 19) return 2;
	const a = array(&values);
	if (a.len != 3 or a[3] != 17) return 3;
	const numeric: [*]u8 = @ptrFromInt(17);
	const e = empty(numeric[0..0]);
	if (e.len != 0 or @intFromPtr(e.ptr) != 17) return 4;
	const p: *u8 = @ptrFromInt(16);
	const q: *align(16) u8 = @alignCast(p);
	if (@intFromPtr(q) != 16) return 5;
	const text: *const [3:0]u8 = &text_backing;
	const aligned_text: *align(16) const [3:0]u8 = @alignCast(text);
	if (address(aligned_text) != 0 or aligned_text[0] != 97) return 6;
	return 0;
}
