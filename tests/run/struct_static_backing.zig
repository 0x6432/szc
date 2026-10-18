// exit: 0
// outlined-data: true
// Scalar-pointer calls perform real volatile loads; no C struct layout assumptions.
const Inner = struct { word: u32, tag: u8 };
const Empty = struct {};
const Root = struct {
    tag: u8,
    wide: u64,
    inner: Inner,
    rows: [2]Inner,
    text: []const u8,
    pointer: *const u8,
    child: *const Inner,
    callback: *const fn (u8) u8,
    sentinel: [2:9]u8,
    empty_sentinel: [0:7]u8,
    flags: bool,
    odd: u9,
    z: [0]u64,
    empty: Empty,
};
const target: u8 = 71;
const child: Inner = .{ .word = 505, .tag = 17 };
fn callback(x: u8) u8 {
    return x + 1;
}
const root: Root align(128) = .{
    .tag = 11,
    .wide = 0x1122334455667788,
    .inner = .{ .word = 0x12345678, .tag = 12 },
    .rows = .{ .{ .word = 101, .tag = 13 }, .{ .word = 202, .tag = 14 } },
    .text = "abc",
    .pointer = &target,
    .child = &child,
    .callback = &callback,
    .sentinel = .{ 21, 22 },
    .empty_sentinel = .{},
    .flags = true,
    .odd = 257,
    .z = .{},
    .empty = .{},
};
const Same = struct { x: u8 };
const named_a: Same = .{ .x = 33 };
const named_b: Same = .{ .x = 33 };
const array = [_]Inner{ .{ .word = 303, .tag = 15 }, .{ .word = 404, .tag = 16 } };

noinline fn read8(p: *const volatile u8) u8 {
    return p.*;
}
noinline fn read9(p: *const volatile u9) u9 {
    return p.*;
}
noinline fn read32(p: *const volatile u32) u32 {
    return p.*;
}
noinline fn read64(p: *const volatile u64) u64 {
    return p.*;
}
noinline fn readBool(p: *const volatile bool) bool {
    return p.*;
}
noinline fn readLen(p: *const volatile usize) usize {
    return p.*;
}
noinline fn readPointer(p: *const volatile *const u8) u8 {
    return p.*.*;
}
noinline fn readSlicePointer(p: *const volatile [*]const u8) u8 {
    return p.*[1];
}
noinline fn invoke(p: *const volatile *const fn (u8) u8) u8 {
    return p.*(7);
}
noinline fn address(p: *const anyopaque) usize {
    return @intFromPtr(p);
}

pub fn main() u8 {
    if (read8(&root.tag) != 11) return 1;
    if (read64(&root.wide) != 0x1122334455667788) return 2;
    if (read32(&root.inner.word) != 0x12345678 or read8(&root.inner.tag) != 12) return 3;
    if (read32(&root.rows[1].word) != 202 or read8(&root.rows[0].tag) != 13) return 4;
    if (read32(&array[1].word) != 404 or read8(&array[0].tag) != 15) return 5;
    if (readLen(&root.text.len) != 3 or readSlicePointer(&root.text.ptr) != 98) return 6;
    if (readPointer(&root.pointer) != 71) return 7;
    if (invoke(&root.callback) != 8) return 8;
    if (read8(&root.sentinel[0]) != 21 or read8(&root.sentinel[2]) != 9) return 9;
    if (read8(&root.empty_sentinel[0]) != 7) return 10;
    if (!readBool(&root.flags) or read9(&root.odd) != 257) return 11;
    const base = address(@ptrCast(&root));
    if (base % 128 != 0) return 12;
    if (address(@ptrCast(&root.tag)) - base != @offsetOf(Root, "tag")) return 13;
    if (address(@ptrCast(&root.inner.word)) - base != @offsetOf(Root, "inner") + @offsetOf(Inner, "word")) return 14;
    if (address(@ptrCast(&root.rows[1].tag)) - base != @offsetOf(Root, "rows") + @sizeOf(Inner) + @offsetOf(Inner, "tag")) return 15;
    if (address(@ptrCast(&root.z)) - base != @offsetOf(Root, "z")) return 16;
    if (address(@ptrCast(&root.empty)) - base != @offsetOf(Root, "empty")) return 17;
    // Logical declaration identity is tested separately: optimizers may merge bytes.
    if (read8(&named_a.x) != 33 or read8(&named_b.x) != 33) return 18;
    const local: Same = .{ .x = 55 };
    if (read8(&local.x) != 55) return 19;
    if (read8(&@field(root, "tag")) != 11) return 20;
    if (read32(&root.child.word) != 505) return 21;
    if (read8(&(Same{ .x = 66 }).x) != 66) return 22;
    return 0;
}
