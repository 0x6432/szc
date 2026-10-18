// exit: 0
const I = struct {
    x: u8 = 7,
    y: u16 = 513,
    const Self = @This();
    fn read(s: Self) u16 {
        return s.x + s.y;
    }
    fn set(s: *Self, n: u8) void {
        s.x = n;
    }
    fn clone(s: Self) Self {
        return s;
    }
    fn deferred(s: *Self) Self {
        defer s.* = .{};
        return s.*;
    }
    fn alias(s: Self, p: *Self) Self {
        p.x = 99;
        return s;
    }
    fn same(s: *Self) *Self {
        return s;
    }
    fn bump(s: *volatile Self) void {
        s.x += 1;
    }
};
const O = struct {
    rows: [2]I = .{ .{}, .{ .x = 4 } },
    tag: u8 = 3,
    fn total(s: O) u16 {
        return s.rows[0].read() + s.rows[1].read();
    }
    fn reset(s: *O) void {
        s.* = .{};
    }
};
var g: O = .{};
noinline fn index() usize {
    return 1;
}
noinline fn call(p: *const fn (I) I, s: I) I {
    return p(s);
}
pub fn main() u8 {
    if (g.rows[0].read() != 520 or g.rows[1].read() != 517 or g.total() != 1037) return 1;
    g.rows[index()].set(9);
    const old = g.rows[1].clone();
    g.rows[1].set(3);
    if (old.x != 9 or g.rows[1].x != 3) return 2;
    const saved = g.rows[1].deferred();
    if (saved.x != 3 or g.rows[1].x != 7 or g.rows[1].y != 513) return 3;
    g.rows[1] = g.rows[1].alias(&g.rows[1]);
    if (g.rows[1].x != 7) return 4;
    g.rows[1].same().set(11);
    if (g.rows[1].x != 11) return 5;
    const via_pointer = call(&I.clone, g.rows[1]);
    g.reset();
    if (via_pointer.x != 11 or g.rows[1].x != 4) return 6;
    var a: [2][1]I = .{ .{.{}}, .{.{ .x = 2 }} };
    a[index()][0].set(8);
    if (a[1][0].clone().read() != 521 or a[0][0].x != 7) return 7;
    const slice: []I = &g.rows;
    slice[index()].set(12);
    if (g.rows[1].x != 12) return 8;
    const temp = (I{}).clone().read();
    if (temp != 520 or @field(I, "read")(I{}) != 520) return 9;
    const v: *volatile I = &g.rows[0];
    v.bump();
    if (v.x != 8 or g.rows[0].x != 8) return 10;
    return 0;
}
