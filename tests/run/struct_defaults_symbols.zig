// exit: 0
var byte: u8 = 7;
var bytes = [_]u8{ 1, 2, 3 };
var calls: u8 = 0;
fn plus(n: u8) u8 {
    calls += 1;
    return n + 1;
}
const I = struct {
    x: u16 = 513,
};
const fixed: I = .{};
const H = struct {
    p: *u8 = &byte,
    s: []u8 = &bytes,
    cb: *const fn (u8) u8 = &plus,
    record: *const I = &fixed,
};
var g: H = .{};
noinline fn identity(h: H) H {
    return h;
}
noinline fn callback(p: *const volatile H) *const fn (u8) u8 {
    return p.cb;
}
noinline fn index() usize {
    return 1;
}
pub fn main() u8 {
    if (g.p.* != 7 or g.s.len != 3 or g.s[2] != 3 or g.record.x != 513) return 1;
    if (callback(&g)(3) != 4 or calls != 1) return 2;
    const old = identity(g);
    g.s = g.s[0..1];
    old.p.* = 9;
    old.s[1] = 7;
    if (g.p.* != 9 or old.s.len != 3 or g.s.len != 1 or bytes[1] != 7) return 3;
    g = .{};
    if (g.s.len != 3 or g.p.* != 9 or g.s[1] != 7) return 4;
    const a: [2]H = .{ .{}, .{} };
    a[index()].p.* = 11;
    if (byte != 11 or a[0].p != a[1].p or a[0].record != a[1].record) return 5;
    var other: u8 = 12;
    const local: H = .{ .p = &other };
    local.p.* = 13;
    if (other != 13 or byte != 11 or local.s.len != 3) return 6;
    const p: *volatile H = &g;
    const len = p.s.len;
    p.s.len = 2;
    if (len != 3 or g.s.len != 2 or p.record.x != 513) return 7;
    const h: H = .{};
    const q = &h.p;
    if (q.*.* != 11 or h.cb(7) != 8 or calls != 2) return 8;
    return 0;
}
