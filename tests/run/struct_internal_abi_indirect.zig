// exit: 0
const S = struct { a: u8, b: u16 };
const F = *const fn (S) S;
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn plus(s: S) S {
    return .{ .a = s.a + 1, .b = s.b + 10 };
}
noinline fn minus(s: S) S {
    return .{ .a = s.a - 1, .b = s.b - 1 };
}
noinline fn choose(x: u8) F {
    return if (x == 1) &plus else &minus;
}
noinline fn callback(p: *const volatile F) F {
    return p.*;
}
const H = struct { callbacks: [2]F, single: F };
const holder: H = .{ .callbacks = .{ &plus, &minus }, .single = &plus };
noinline fn mutate(p: *S) u8 {
    p.a = 9;
    return 3;
}
noinline fn first(s: S, n: u8) u8 {
    _ = n;
    return s.a;
}
noinline fn selectFirst(x: u8) *const fn (S, u8) u8 {
    _ = x;
    return &first;
}
noinline fn captured(s: S, p: *S) S {
    p.a = 55;
    return s;
}
noinline fn getCaptured() *const fn (S, *S) S {
    return &captured;
}
noinline fn idArray(a: [2]S) [2]S {
    return a;
}
noinline fn getArray() *const fn ([2]S) [2]S {
    return &idArray;
}
noinline fn mix(x: u8, s: S, y: u16) S {
    return .{ .a = x + s.a, .b = y + s.b };
}
noinline fn getMix() *const fn (u8, S, u16) S {
    return &mix;
}
pub fn main() u8 {
    const input: S = .{ .a = seed(7), .b = 20 };
    const a = choose(seed(1))(input);
    const b = choose(seed(0))(input);
    if (a.a != 8 or a.b != 30 or b.a != 6 or b.b != 19) return 1;
    const c = callback(&holder.single)(input);
    const d = callback(&holder.callbacks[1])(input);
    if (c.a != 8 or d.a != 6) return 2;
    const e = holder.callbacks[seed(0)](input);
    if (e.b != 30) return 3;
    var s: S = .{ .a = seed(1), .b = 2 };
    if (selectFirst(seed(0))(s, mutate(&s)) != 1 or s.a != 9) return 4;
    s = S{ .a = seed(3), .b = 4 };
    s = getCaptured()(s, &s);
    if (s.a != 3 or s.b != 4) return 5;
    const rows = getArray()(.{ input, s });
    s.a = 99;
    if (rows[0].a != 7 or rows[1].a != 3) return 6;
    const mixed = getMix()(seed(1), input, 5);
    if (mixed.a != 8 or mixed.b != 25) return 7;
    if (@intFromPtr(holder.single) == 0 or @intFromPtr(holder.callbacks[1]) == 0) return 8;
    return 0;
}
