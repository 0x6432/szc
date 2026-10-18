// exit: 0
const S = struct {
    a: u8 = 9,
    b: u8 = 8,
    c: u8 = 7,
};
var g: S = .{ .a = 1, .b = 2, .c = 3 };
const I = struct {
    a: u8 = 9,
    b: u8 = 8,
};
const O = struct {
    i: I = .{},
    tag: u8 = 7,
};
var o: O = .{ .i = .{ .a = 1, .b = 2 }, .tag = 3 };
noinline fn reset() void {
    g = .{ .a = 1, .b = 2, .c = 3 };
}
noinline fn stop() void {
    g = .{ .b = 5, .c = return };
}
noinline fn identity(s: S) S {
    return s;
}
pub fn main() u8 {
    g = .{ .b = g.a };
    if (g.a != 9 or g.b != 1 or g.c != 7) return 1;
    reset();
    g = .{ .c = g.b, .b = g.a };
    if (g.a != 9 or g.b != 1 or g.c != 2) return 2;
    reset();
    g = .{ .a = g.b, .b = g.a };
    if (g.a != 2 or g.b != 2 or g.c != 7) return 3;
    reset();
    g = S{ .a = g.b, .b = g.a };
    if (g.a != 2 or g.b != 1 or g.c != 7) return 4;
    reset();
    g = @as(S, .{ .a = g.b, .b = g.a });
    if (g.a != 2 or g.b != 1 or g.c != 7) return 5;
    reset();
    stop();
    if (g.a != 1 or g.b != 5 or g.c != 3) return 6;
    o = .{ .tag = o.i.a };
    if (o.i.a != 9 or o.i.b != 8 or o.tag != 1) return 7;
    const old = identity(g);
    g = .{};
    if (old.a != 1 or old.b != 5 or old.c != 3 or g.a != 9 or g.b != 8 or g.c != 7) return 8;
    return 0;
}
