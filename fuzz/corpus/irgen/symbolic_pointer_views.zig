// exit: 0
const word: u32 = 0x78563412;
const data: [*]const u8 = @ptrCast(&word);
const pairs = [2]u16{0x1122,0x3344};
const selected: *const u8 = @ptrCast(&pairs[1]);
var writable = [2]u16{0x1122,0x3344};
const target: [*]u8 = @ptrCast(&writable[1]);
const text: []const u8 = "hello";
const descriptor: *const [2]usize = @ptrCast(&text);
const addresses = [2]*const u32{&word,&word};
const relocation: *const usize = @ptrCast(&addresses[1]);
fn increment(n: u8) u8 { return n + 1; }
fn other(n: u8) u8 { return n + 2; }
var callback: *const u8 = @ptrCast(&other);
const function_data: *const u8 = @ptrCast(&increment);
const function_again: *const fn(u8) u8 = @ptrCast(function_data);
fn get(p: *const u8) u8 { return p.*; }
fn read(p: [*]const u8, i: usize) u8 { return p[i]; }
fn copy(p: *const [4]u8) [4]u8 { return p.*; }
fn address(p: *const usize) usize { return p.*; }
fn field(p: *const [2]usize, i: usize) usize { return p[i]; }
fn read_word(p: *const u32) u32 { return p.*; }
fn volatile_byte(p: *const volatile u8) u8 { return p.*; }
pub fn main() u8 {
	if (read(data,0) != 0x12 or read(data,1) != 0x34 or read(data,3) != 0x78) return 1;
	if (get(selected) != 0x44) return 2;
	target[0] = 0x77;
	if (writable[1] != 0x3377 or target[0] != 0x77 or target[1] != 0x33) return 3;
	const view: *const [4]u8 = @ptrCast(&word);
	const copied = copy(view);
	if (copied[0] != 0x12 or copied[3] != 0x78) return 4;
	if (field(descriptor,1) != 5 or field(descriptor,0) != @intFromPtr(text.ptr)) return 5;
	if (address(relocation) != @intFromPtr(&word)) return 6;
	const dynamic_callback: *const fn(u8) u8 = @ptrCast(callback);
	if (function_again(3) != 4 or dynamic_callback(5) != 7) return 7;
	const restored: *const u32 = @alignCast(@ptrCast(data));
	if (read_word(restored) != word) return 8;
	const window = data[1..3];
	if (read(window,0) != 0x34 or read(window,1) != 0x56) return 9;
	const volatile_view: *const volatile u8 = @ptrCast(&word);
	if (volatile_byte(volatile_view) != 0x12) return 10;
	return 0;
}
