// exit: 0
fn bytes(p: *u32) [*]u8 { return @ptrCast(p); }
fn word(p: *align(1) const volatile u8) *u32 {
	return @volatileCast(@constCast(@alignCast(@ptrCast(p))));
}
fn signed(p: *u32) *i32 { return @ptrCast(p); }
fn scalar(p: [*]u8) *u8 { return @ptrCast(p); }
fn sentinel(p: [*]u8) [*:0]u8 { return @ptrCast(p); }
fn array(p: [*]u8) *[4]u8 { return @ptrCast(p); }
fn checked(p: *allowzero u8) *i8 { return @ptrCast(p); }
fn numeric(p: *allowzero u8) *allowzero u16 { return @alignCast(@ptrCast(p)); }
fn add(n: u8) u8 { return n + 7; }
fn data(p: *const fn(u8) u8) *const u8 { return @ptrCast(p); }
fn function(p: *const u8) *const fn(u8) u8 { return @ptrCast(p); }
fn read_bytes(p: *const [4]u8) u32 {
	const q: *align(1) const u32 = @ptrCast(p);
	return q.*;
}
fn realign(p: *u8) *align(64) u16 { return @alignCast(@ptrCast(p)); }
var aligned: [8]u8 align(64) = .{1,2,3,4,5,6,7,8};
pub fn main() u8 {
	var n: u32 = 0x78563412;
	const b = bytes(&n);
	if (b[0] != 0x12 or b[1] != 0x34 or b[2] != 0x56 or b[3] != 0x78) return 1;
	b[1] = 0xab;
	if (n != 0x7856ab12 or word(&b[0]).* != n) return 2;
	signed(&n).* = -1;
	if (n != 0xffffffff) return 3;
	var a = [4]u8{7,9,11,0};
	const m: [*]u8 = &a;
	if (scalar(m).* != 7 or sentinel(m)[3] != 0) return 4;
	array(m)[2] = 19;
	if (a[2] != 19) return 5;
	if (checked(&a[0]).* != 7) return 6;
	if (@intFromPtr(numeric(@ptrFromInt(0))) != 0) return 7;
	if (@intFromPtr(numeric(@ptrFromInt(16))) != 16) return 8;
	if (function(data(&add))(3) != 10) return 9;
	if (read_bytes(&a) != 0x00130907) return 10;
	if (realign(&aligned[0]).* != 0x0201) return 11;
	const p: *allowzero u8 = @ptrFromInt(256);
	const q: [*:17]u8 = @ptrCast(p);
	if (@intFromPtr(q) != 256) return 12;
	return 0;
}
