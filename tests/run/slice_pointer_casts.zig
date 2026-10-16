// exit: 0
fn many(s: []u8) [*]u8 { return @ptrCast(s); }
fn scalar(s: []const u8) *const u8 { return @ptrCast(s); }
fn word(s: []u8) *u16 { return @alignCast(@ptrCast(s)); }
fn nullable(s: []allowzero u8) *allowzero u16 { return @alignCast(@ptrCast(s)); }
fn nonnull(s: []allowzero u8) *i8 { return @ptrCast(s); }
fn qualified(s: []const volatile u8) *u16 {
	return @volatileCast(@alignCast(@constCast(@ptrCast(s))));
}
var backing: [4]u8 align(16) = .{1,2,3,4};
pub fn main() u8 {
	const s: []u8 = &backing;
	many(s)[1] = 9;
	if (backing[1] != 9 or scalar(s).* != 1) return 1;
	if (word(s).* != 0x0901 or qualified(s).* != 0x0901) return 2;
	const unaligned: [*]u8 = @ptrFromInt(17);
	const empty: []u8 = unaligned[0..0];
	if (@intFromPtr(many(empty)) != 17) return 3;
	const aligned: [*]allowzero u8 = @ptrFromInt(16);
	const e: []allowzero u8 = aligned[0..0];
	if (@intFromPtr(nullable(e)) != 16 or @intFromPtr(nonnull(e)) != 16) return 4;
	const zero: [*]allowzero u8 = @ptrFromInt(0);
	const z: []allowzero u8 = zero[0..0];
	if (@intFromPtr(nullable(z)) != 0) return 5;
	const p: *allowzero i8 = @ptrCast(z);
	if (@intFromPtr(p) != 0) return 6;
	const q: [*:17]u8 = @ptrCast(e);
	if (@intFromPtr(q) != 16) return 7;
	const known_null: *i8 = @ptrCast(z);
	if (@intFromPtr(known_null) != 0) return 8;
	return 0;
}
