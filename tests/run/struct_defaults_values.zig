// exit: 0
const I = struct {
    x: u8 = 7,
    y: u16 = 513,
};
const S = struct {
    i: I = .{},
    rows: [2]I = .{ .{}, .{ .x = 4 } },
    bytes: [2:9]u8 = .{ 1, 2 },
    z: [0]u64 = .{},
    u: u8 = undefined,
};
var g: S = .{};
noinline fn identity(s: S) S {
    return s;
}
noinline fn change() u8 {
    g.i.x = 99;
    return 1;
}
noinline fn first(s: S, n: u8) u8 {
    _ = n;
    return s.i.x;
}
noinline fn alias(s: S, p: *S) S {
    p.i.x = 99;
    return s;
}
noinline fn returned(p: *S) S {
    defer p.* = .{};
    return p.*;
}
noinline fn index() usize {
    return 1;
}
pub fn main() u8 {
    if (g.i.x != 7 or g.i.y != 513 or g.rows[1].x != 4 or g.bytes[2] != 9) return 1;
    g.u = 5;
    const old = identity(g);
    g.i.x = 3;
    g.rows[1].y = 9;
    if (old.i.x != 7 or old.rows[1].y != 513 or old.u != 5) return 2;
    g = .{};
    g.u = 5;
    if (first(g, change()) != 7 or g.i.x != 99) return 3;
    g = .{};
    g.u = 5;
    g = alias(g, &g);
    if (g.i.x != 7 or g.u != 5) return 4;
    g.i.x = 3;
    g.u = 5;
    const ret = returned(&g);
    if (ret.i.x != 3 or ret.u != 5 or g.i.x != 7) return 5;
    var a: [2]S = .{ .{}, .{ .i = .{ .x = 2 } } };
    a[0].u = 1;
    a[1].u = 2;
    const saved = a[index()];
    a[index()].i = .{};
    if (saved.i.x != 2 or saved.i.y != 513 or a[1].i.x != 7) return 6;
    a[1].rows[0] = .{ .y = 9 };
    if (a[1].rows[0].x != 7 or a[1].rows[0].y != 9) return 7;
    const p: *volatile S = &g;
    p.i.y = 1025;
    if (p.i.y != 1025 or g.z.len != 0) return 8;
    const n: u8 = 11;
    const L = struct {
        x: u8 = n,
        a: [2]u8 = .{ n, 3 },
    };
    var l: L = .{};
    l.a[1] = 4;
    if (l.x != 11 or l.a[0] != 11 or l.a[1] != 4) return 9;
    const Z = struct {
        z: [0:9]u8 = .{},
        v: void = {},
        n: u0 = 0,
    };
    var zero: Z = .{};
    zero.n = 0;
    if (zero.z[0] != 9 or zero.n != 0) return 10;
    return 0;
}
