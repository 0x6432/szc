// exit: 0
const E = struct {};
const I = struct { x: u16, tag: u8 };
const O = struct { rows: [2]I, bytes: [2:9]u8, empty_sentinel: [0:7]u8, text: []u8, z: [0]u64, empty: E, v: void, zero: u0 };
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn identity(o: O) O {
    return o;
}
noinline fn array(a: [2][1]I) [2][1]I {
    return a;
}
noinline fn mutate(p: *[2][1]I) u8 {
    p[0][0].x = 99;
    return 1;
}
noinline fn first(a: [2][1]I, n: u8) u16 {
    _ = n;
    return a[0][0].x;
}
noinline fn capture(p: *[2][1]I) [2][1]I {
    defer p[0][0].x = 77;
    return p.*;
}
noinline fn zero(a: [2][0]I) [2][0]I {
    return a;
}
noinline fn empty(e: E) E {
    return e;
}
noinline fn zeroOuter(a: [0][2]I) [0][2]I {
    return a;
}
noinline fn branch(a: I, b: I, n: u8) I {
    return if (n == 1) a else b;
}
noinline fn block(a: [2]I, n: u8) I {
    return blk: {
        if (n == 1) break :blk a[1];
        break :blk a[0];
    };
}
noinline fn aligned(o: O) O {
    var copy: O align(64) = o;
    copy.rows[0].tag += 1;
    return copy;
}
pub fn main() u8 {
    var text = [_]u8{ seed(1), 2, 3 };
    var o: O = .{ .rows = .{ .{ .x = seed(11), .tag = 1 }, .{ .x = 22, .tag = 2 } }, .bytes = .{ 3, 4 }, .empty_sentinel = .{}, .text = &text, .z = .{}, .empty = .{}, .v = {}, .zero = 0 };
    const saved = identity(o);
    o.rows[0].x = 55;
    o.text = o.text[0..1];
    if (saved.rows[0].x != 11 or saved.text.len != 3) return 1;
    saved.text[0] = 9;
    if (text[0] != 9) return 2;
    if (saved.bytes[2] != 9 or saved.empty_sentinel[0] != 7) return 3;
    var a: [2][1]I = .{ .{.{ .x = seed(1), .tag = 2 }}, .{.{ .x = 3, .tag = 4 }} };
    if (first(a, mutate(&a)) != 1 or a[0][0].x != 99) return 4;
    const value = array(a);
    a[1][0].x = 88;
    if (value[1][0].x != 3) return 5;
    const returned = capture(&a);
    if (returned[0][0].x != 99 or a[0][0].x != 77) return 6;
    a = capture(&a);
    if (a[0][0].x != 77 or a[1][0].x != 88) return 7;
    if (zero(.{ .{}, .{} }).len != 2 or zeroOuter(.{}).len != 0) return 8;
    _ = empty(.{});
    const chosen = branch(o.rows[0], o.rows[1], seed(1));
    const broken = block(o.rows, seed(1));
    o.rows[0].x = 66;
    o.rows[1].x = 67;
    if (chosen.x != 55 or broken.x != 22) return 9;
    const over = aligned(o);
    if (over.rows[0].tag != 2 or o.rows[0].tag != 1 or over.z.len != 0) return 10;
    return 0;
}
