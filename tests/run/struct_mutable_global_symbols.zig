// exit: 0
// outlined-data: true
const S = struct { a: u8, b: u16 };
const F = *const fn (S) S;
const N = struct { peer: *N, record: *S, callback: F, text: []u8, zero: [0]S, end: [0:7]u8 };
var left: S = .{ .a = 7, .b = 513 };
var right: S = .{ .a = 7, .b = 513 };
var bytes = [_]u8{ 1, 2, 3 };
var first: N align(128) = .{ .peer = &second, .record = &left, .callback = &inc, .text = &bytes, .zero = .{}, .end = .{} };
var second: N = .{ .peer = &first, .record = &right, .callback = &dec, .text = &bytes, .zero = .{}, .end = .{} };
const Self = struct { p: *Self, x: u8 };
var self: Self = .{ .p = &self, .x = 5 };
noinline fn inc(s: S) S {
    return .{ .a = s.a + 1, .b = s.b + 1 };
}
noinline fn dec(s: S) S {
    return .{ .a = s.a - 1, .b = s.b - 1 };
}
noinline fn callback(p: *const volatile F) F {
    return p.*;
}
noinline fn address(p: *const anyopaque) usize {
    return @intFromPtr(p);
}
pub fn main() u8 {
    if (&left == &right or address(@ptrCast(&left)) == address(@ptrCast(&right))) return 1;
    first.peer.peer.record.a = 9;
    if (left.a != 9 or right.a != 7) return 2;
    self.p.x = 6;
    if (self.x != 6 or self.p != &self) return 3;
    const up = callback(&first.callback)(left);
    const down = callback(&second.callback)(right);
    if (up.a != 10 or up.b != 514 or down.a != 6 or down.b != 512) return 4;
    first.callback = &dec;
    const replaced = callback(&first.callback)(left);
    if (replaced.a != 8 or replaced.b != 512) return 5;
    const saved = first;
    const vp: *volatile N = &first;
    const old_len = vp.text.len;
    vp.text.len = 2;
    if (old_len != 3 or vp.text.len != 2 or saved.text.len != 3) return 6;
    const new_ptr: [*]u8 = &bytes;
    vp.text.ptr = new_ptr;
    saved.text[1] = 8;
    if (vp.text.ptr[1] != 8 or bytes[1] != 8) return 7;
    if (first.end[0] != 7 or first.zero.len != 0) return 8;
    if (address(@ptrCast(&first)) % 128 != 0 or address(@ptrCast(&first.record)) - address(@ptrCast(&first)) != @offsetOf(N, "record")) return 9;
    if (@TypeOf(&first.record) != **S or @TypeOf(&vp.text.len) != *volatile usize) return 10;
    return 0;
}
