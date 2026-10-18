// exit: 0
const S = struct { a: u8, b: u8 };
var s: S = .{ .a = 1, .b = 2 };
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn contextualPartial() u8 {
    s = .{ .a = 7, .b = return s.a };
}
noinline fn explicitPartial() u8 {
    s = S{ .a = 7, .b = return s.a };
}
pub fn main() u8 {
    s.a = seed(1);
    s = .{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 2) return 1;
    s = S{ .a = 1, .b = 2 };
    s = S{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 1) return 2;
    s = S{ .a = 1, .b = 2 };
    if (contextualPartial() != 7 or s.a != 7 or s.b != 2) return 3;
    s = S{ .a = 1, .b = 2 };
    if (explicitPartial() != 1 or s.a != 1 or s.b != 2) return 4;
    return 0;
}
