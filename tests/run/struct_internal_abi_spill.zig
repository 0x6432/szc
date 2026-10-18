// exit: 0
const S = struct { a: u64, b: u8 };
const F = *const fn (u64, S, u64, S, u64, S, u64, u64) S;
noinline fn seed(x: u64) u64 {
    return x;
}
noinline fn combine(a: u64, s: S, b: u64, t: S, c: u64, u: S, d: u64, e: u64) S {
    return .{ .a = a + s.a + b + t.a + c + u.a + d + e, .b = s.b + t.b + u.b };
}
noinline fn select(x: u64) F {
    _ = x;
    return &combine;
}
pub fn main() u8 {
    const a: S = .{ .a = seed(2), .b = 3 };
    const b: S = .{ .a = seed(4), .b = 5 };
    const c: S = .{ .a = seed(6), .b = 7 };
    const direct = combine(seed(1), a, 3, b, 5, c, 7, 8);
    if (direct.a != 36 or direct.b != 15) return 1;
    const indirect = select(seed(0))(1, a, 3, b, 5, c, 7, 8);
    if (indirect.a != 36 or indirect.b != 15) return 2;
    return 0;
}
