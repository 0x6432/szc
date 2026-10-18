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
var left: S = .{ .x = 1 };
var right: S = .{ .x = 9 };
var p: *S = &left;
var count: u8 = 0;
noinline fn move() u8 {
    p = &right;
    return 1;
}
noinline fn root() *S {
    count += 1;
    return p;
}
var first_calls: u8 = 0;
var second_calls: u8 = 0;
fn first(n: u8) u8 {
    first_calls += 1;
    return n + 1;
}
fn second(n: u8) u8 {
    second_calls += 1;
    return n + 10;
}
const H = struct {
    cb: *const fn (u8) u8,
};
var h: H = .{ .cb = &first };
var other: H = .{ .cb = &second };
var q: *H = &h;
var a: [2]H = .{ .{ .cb = &second }, .{ .cb = &first } };
var index_calls: u8 = 0;
noinline fn replace() u8 {
    h.cb = &second;
    return 3;
}
noinline fn moveHolder() u8 {
    q = &other;
    return 3;
}
noinline fn idx() usize {
    index_calls += 1;
    return 1;
}
noinline fn replaceAt() u8 {
    a[1].cb = &second;
    return 3;
}
pub fn main() u8 {
    if (p.read(move()) != 1 or p != &right) return 1;
    p = &left;
    if (p.value(move()) != 1 or p != &right) return 2;
    p = &left;
    if (p.bump(move()) != 2 or left.x != 2 or right.x != 9) return 3;
    left.x = 1;
    p = &left;
    count = 0;
    if (root().read(move()) != 1 or count != 1 or p != &right) return 4;
    if (h.cb(replace()) != 4 or first_calls != 1 or second_calls != 0) return 5;
    h.cb = &first;
    if ((h.cb)(replace()) != 4 or first_calls != 2 or second_calls != 0) return 6;
    h.cb = &first;
    if (@field(h, "cb")(replace()) != 4 or first_calls != 3 or second_calls != 0) return 7;
    if (a[idx()].cb(replaceAt()) != 4 or index_calls != 1 or first_calls != 4 or second_calls != 0) return 8;
    h.cb = &first;
    q = &h;
    if (q.cb(moveHolder()) != 4 or q != &other or first_calls != 5 or second_calls != 0) return 9;
    if (h.cb(2) != 3 or q.cb(2) != 12 or first_calls != 6 or second_calls != 1) return 10;
    return 0;
}
