// exit: 0
const S = struct { a: u8, b: u8 };
var s: S = .{ .a = 1, .b = 2 };
noinline fn reset() void {
    s = S{ .a = 1, .b = 2 };
}
noinline fn contextualExit() u8 {
    reset();
    s = .{ .a = 7, .b = return s.a };
    return 99;
}
noinline fn explicitExit() u8 {
    reset();
    s = S{ .a = 7, .b = return s.a };
    return 99;
}
pub fn main() u8 {
    s = .{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 2) return 1;
    reset();
    s = S{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 1) return 2;
    reset();
    s = @as(S, .{ .a = s.b, .b = s.a });
    if (s.a != 2 or s.b != 1) return 3;
    reset();
    const t: S = .{ .a = s.b, .b = s.a };
    s = t;
    if (s.a != 2 or s.b != 1) return 4;
    if (contextualExit() != 7 or s.a != 7) return 5;
    if (explicitExit() != 1 or s.a != 1) return 6;
    return 0;
}
