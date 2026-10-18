// exit: 0
const E = struct {};
const I = struct { x: u16, tag: u8 };
const O = struct { rows: [2]I, bytes: [2:9]u8, empty_sentinel: [0:7]u8, text: []u8, z: [0]u64, e: E, v: void, zero: u0 };
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn index(x: usize) usize {
    return x;
}
noinline fn addr(p: *const anyopaque) usize {
    return @intFromPtr(p);
}
noinline fn edit(p: *I) u16 {
    p.x += 10;
    return p.x;
}
pub fn main() u8 {
    var text = [_]u8{ seed(3), 4, 5 };
    var s: O align(64) = .{ .rows = .{ .{ .x = seed(11), .tag = 1 }, .{ .x = 22, .tag = 2 } }, .bytes = .{ 3, 4 }, .empty_sentinel = .{}, .text = &text, .z = .{}, .e = .{}, .v = {}, .zero = 0 };
    const rows = s.rows;
    s.rows[index(1)].x = 99;
    if (rows[1].x != 22 or s.rows[1].x != 99) return 1;
    const row = s.rows[index(0)];
    if (edit(&s.rows[index(0)]) != 21 or row.x != 11) return 2;
    const view: []I = &s.rows;
    view[index(1)].tag = 7;
    if (s.rows[1].tag != 7 or view.len != 2) return 3;
    var a = [_]I{ .{ .x = seed(1), .tag = 3 }, .{ .x = 2, .tag = 4 } };
    const before = a;
    a[index(1)].x = 55;
    if (before[1].x != 2 or a[1].x != 55) return 4;
    if (addr(@ptrCast(&a[1])) - addr(@ptrCast(&a[0])) != @sizeOf(I)) return 5;
    const slice_copy = s;
    s.text = s.text[0..1];
    if (slice_copy.text.len != 3 or s.text.len != 1) return 6;
    slice_copy.text[0] = 8;
    if (text[0] != 8) return 7; // slice value snapshot does not deep-copy its pointee
    s.bytes[index(1)] = 6;
    if (s.bytes[2] != 9 or s.empty_sentinel[0] != 7) return 8;
    s.e = E{};
    s.zero = 0;
    s.v = {};
    if (s.z.len != 0 or addr(@ptrCast(&s)) % 64 != 0) return 9;
    const choice: I = if (seed(1) == 1) s.rows[0] else s.rows[1];
    s.rows[0].x = 66;
    if (choice.x != 21) return 10;
    const block: I = blk: {
        if (seed(1) == 1) break :blk s.rows[1];
        break :blk s.rows[0];
    };
    s.rows[1].x = 77;
    if (block.x != 99) return 11;
    var matrix: [2][1]I = .{ .{.{ .x = seed(12), .tag = 1 }}, .{.{ .x = 13, .tag = 2 }} };
    matrix[index(1)][0].x = 88;
    if (matrix[0][0].x != 12 or matrix[1][0].x != 88) return 12;
    return 0;
}
