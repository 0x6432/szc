// exit: 0
const scalar: u32 = 0x12345678;
const erased: *const u8 = @ptrCast(&scalar);
const restored: *const u32 = @alignCast(@ptrCast(erased));
const scalar_value = restored.*;
const wide: u33 = 0x1ffffffff;
const wide_bytes: *const u8 = @ptrCast(&wide);
const wide_again: *const u33 = @alignCast(@ptrCast(wide_bytes));
const wide_value = wide_again.*;
const array: [2]u16 = .{17, 29};
const array_bytes: [*]const u8 = @ptrCast(&array);
const array_again: *const [2]u16 = @alignCast(@ptrCast(array_bytes));
const array_value = array_again.*;
const element_bytes: *const u8 = @ptrCast(&array[1]);
const element_again: *const u16 = @alignCast(@ptrCast(element_bytes));
const element_value = element_again.*;
const slice: []const u16 = &array;
const descriptor_bytes: *const u8 = @ptrCast(&slice);
const descriptor_again: *const []const u16 = @alignCast(@ptrCast(descriptor_bytes));
const descriptor_length = descriptor_again.*.len;
const string_bytes: *const anyopaque = @ptrCast("abc");
const string_again: *const [3:0]u8 = @ptrCast(string_bytes);
const string_value = string_again.*;
var global: u32 = 5;
const global_bytes: *u8 = @ptrCast(&global);
const global_again: *u32 = @alignCast(@ptrCast(global_bytes));
pub fn main() u8 {
	if (scalar_value != scalar or wide_value != wide) return 1;
	if (array_value[0] != 17 or array_value[1] != 29) return 2;
	if (element_value != 29 or descriptor_length != 2) return 3;
	if (string_value[2] != 99 or string_value[3] != 0) return 4;
	global_again.* = 9;
	if (global != 9) return 5;
	return 0;
}
