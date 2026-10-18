// exit: 0
const S = struct {
    x: u8,
    fn value(s: S, n: u8) u8 {
        _ = n;
        return s.x;
    }
    fn read(s: *const S, n: u8) u8 {
        _ = n;
        return s.x;
    }
    fn bump(s: *S, n: u8) u8 {
        s.x += n;
        return s.x;
    }
};
var g: S = .{ .x = 1 };
var count: u8 = 0;
var trace: u32 = 0;
var a: [2]S = .{ .{ .x = 2 }, .{ .x = 3 } };
noinline fn root() *S {
    count += 1;
    return &g;
}
noinline fn make() S {
    count += 1;
    return g;
}
noinline fn mutate() u8 {
    g.x = 9;
    return 1;
}
noinline fn idx() usize {
    trace = trace * 10 + 1;
    return 1;
}
noinline fn arg() u8 {
    trace = trace * 10 + 2;
    return 4;
}
pub fn main() u8 {
    if (root().value(mutate()) != 1 or g.x != 9 or count != 1) return 1;
    g.x = 1;
    count = 0;
    if (root().read(mutate()) != 9 or count != 1) return 2;
    g.x = 1;
    count = 0;
    if (root().bump(mutate()) != 10 or g.x != 10 or count != 1) return 3;
    g.x = 1;
    count = 0;
    if (make().value(mutate()) != 1 or count != 1) return 4;
    g.x = 1;
    count = 0;
    if (make().read(mutate()) != 1 or g.x != 9 or count != 1) return 5;
    if (a[idx()].bump(arg()) != 7 or trace != 12 or a[0].x != 2 or a[1].x != 7) return 6;
    g.x = 1;
    count = 0;
    if (S.value(root().*, mutate()) != 1 or count != 1 or g.x != 9) return 7;
    return 0;
}
