// exit: 0
var last: *u8 = @ptrFromInt(18446744073709551615);
const fixed: *allowzero u32 = @ptrFromInt(0);
var addresses: [2]*u8 = .{@ptrFromInt(16),@ptrFromInt(32)};
fn restore_zero(n: usize) *allowzero u32 { return @ptrFromInt(n); }
fn restore_packed(n: usize) *align(1) u32 { return @ptrFromInt(n); }
fn restore(n: usize) *u16 { return @ptrFromInt(n); }
fn restore_many(n: usize) [*]u16 { return @ptrFromInt(n); }
fn restore_function(n: usize) *const fn () u8 { return @ptrFromInt(n); }
fn seven() u8 { return 7; }
fn known() u8 {
    const p: [*]u16 = @ptrFromInt(16);
    const q: [*]u16 = @ptrFromInt(8);
    const view = p[0..2];
    if (@intFromPtr(p+2) != 20 or @intFromPtr(p-1) != 14 or p-q != 4) return 1;
    if (@intFromPtr(&view[1]) != 18) return 2;
    const z: *u0 = @ptrFromInt(1);
    if (z.* != 0 or @intFromPtr(z) != 1) return 3;
    if (@intFromPtr(fixed) != 0 or @intFromPtr(addresses[1]) != 32) return 4;
    return 0;
}
pub fn main() u8 {
    if (known() != 0) return 1;
    if (@intFromPtr(last) != 18446744073709551615) return 5;
    if (@intFromPtr(restore_zero(0)) != 0 or @intFromPtr(restore_packed(1)) != 1) return 6;
    var data = [3]u16{7,9,11};
    const p = restore(@intFromPtr(&data[0]));
    p.* = 13;
    if (data[0] != 13) return 2;
    const many = restore_many(@intFromPtr(&data));
    many[1] = 15;
    if (data[1] != 15 or many[2] != 11) return 3;
    if (restore_function(@intFromPtr(&seven))() != 7) return 4;
    return 0;
}
