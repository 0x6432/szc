// exit: 0
// Actual effects and destination overwrites; no by-value struct signatures.
const S = struct { a: u8, b: u8 };
const O = struct { inner: S, tag: u8 };
var trace: u32 = 0;
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn mark(x: u8) u8 {
    trace = trace * 10 + x;
    return x;
}
noinline fn field(p: *S) *u8 {
    trace += 1;
    return &p.a;
}
noinline fn target(p: *S) *S {
    trace = trace * 10 + 1;
    return p;
}
noinline fn contextualExit() u8 {
    var s: S = .{ .a = seed(1), .b = 2 };
    s = .{ .a = 7, .b = return s.a };
    return 99;
}
noinline fn explicitExit() u8 {
    var s: S = .{ .a = seed(1), .b = 2 };
    s = S{ .a = 7, .b = return s.a };
    return 99;
}
pub fn main() u8 {
    var s: S = .{ .b = mark(2), .a = mark(1) };
    if (trace != 21) return 1;
    s = S{ .a = seed(1), .b = 2 };
    const copy = s;
    const scalar = s.a;
    s.a = 7;
    if (copy.a != 1 or copy.b != 2 or scalar != 1) return 2;
    s = S{ .a = seed(1), .b = 2 };
    s = .{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 2) return 3;
    s = S{ .a = seed(1), .b = 2 };
    s = S{ .a = s.b, .b = s.a };
    if (s.a != 2 or s.b != 1) return 4;
    s = S{ .a = seed(1), .b = 2 };
    s = @as(S, .{ .a = s.b, .b = s.a });
    if (s.a != 2 or s.b != 1) return 5;
    s = S{ .a = seed(1), .b = 2 };
    const temp: S = .{ .a = s.b, .b = s.a };
    s = temp;
    if (s.a != 2 or s.b != 1) return 6;
    s = s;
    if (s.a != 2 or s.b != 1) return 7;
    var o: O = .{ .inner = .{ .a = seed(1), .b = 2 }, .tag = 3 };
    const nested = o.inner;
    o = .{ .inner = .{ .a = o.inner.b, .b = o.inner.a }, .tag = o.tag };
    if (nested.a != 1 or nested.b != 2 or o.inner.a != 2 or o.inner.b != 2) return 8;
    if (contextualExit() != 7 or explicitExit() != 1) return 9;
    const p = &s.a;
    s.a = 5;
    p.* += 1;
    if (s.a != 6) return 10;
    trace = 0;
    field(&s).* += 2;
    if (trace != 1 or s.a != 8) return 11;
    trace = 0;
    target(&s).* = S{ .a = mark(2), .b = mark(3) };
    if (trace != 123 or s.a != 2 or s.b != 3) return 12;
    // Discarding a complete struct value must not discard its field effects.
    trace = 0;
    _ = S{ .a = mark(4), .b = mark(5) };
    if (trace != 45) return 13;
    return 0;
}
