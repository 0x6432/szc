// exit: 0
const I = struct { x: u16, tag: u8 };
const G = struct { a: u8, rows: [2]I, bytes: [2:9]u8, text: []u8, p: *u8 };
var byte: u8 = 7;
var text = [_]u8{ 3, 4, 5 };
var g: G align(64) = .{ .a = 1, .rows = .{ .{ .x = 11, .tag = 1 }, .{ .x = 22, .tag = 2 } }, .bytes = .{ 6, 7 }, .text = &text, .p = &byte };
var matrix: [2][1]I = .{ .{.{ .x = 31, .tag = 3 }}, .{.{ .x = 41, .tag = 4 }} };
var zero: [0]I = .{};
const alias: *G = &g;
noinline fn index(n: usize) usize {
    return n;
}
noinline fn root() *G {
    return &g;
}
noinline fn mutate() u8 {
    g.a = 9;
    return 1;
}
noinline fn first(value: G, n: u8) u8 {
    _ = n;
    return value.a;
}
noinline fn capture() G {
    defer g.a = 99;
    return g;
}
noinline fn read(p: *const volatile u16) u16 {
    return p.*;
}
pub fn main() u8 {
    if (g.a != 1 or matrix[1][0].x != 41 or zero.len != 0) return 1;
    const old = g;
    const row = g.rows[index(1)];
    alias.rows[index(1)].x = 77;
    root().a = 5;
    if (old.a != 1 or old.rows[1].x != 22 or row.x != 22 or g.rows[1].x != 77) return 2;
    if (first(g, mutate()) != 5 or g.a != 9) return 3;
    const captured = capture();
    if (captured.a != 9 or g.a != 99) return 4;
    g = old;
    if (g.a != 1 or g.rows[1].x != 22) return 5;
    const vp: *volatile G = &g;
    vp.rows[index(0)].x = 513;
    if (read(&g.rows[0].x) != 513 or vp.a != 1) return 6;
    const before = matrix;
    matrix[index(1)][0].x = 88;
    if (before[1][0].x != 41 or matrix[1][0].x != 88) return 7;
    const snapshot = g;
    g.text = g.text[0..1];
    snapshot.text[0] = 8;
    snapshot.p.* = 10;
    if (snapshot.text.len != 3 or g.text.len != 1 or text[0] != 8 or byte != 10) return 8;
    if (g.bytes[2] != 9 or @intFromPtr(&g) % 64 != 0 or @TypeOf(&g.a) != *u8 or @TypeOf(&alias.a) != *u8) return 9;
    return 0;
}
