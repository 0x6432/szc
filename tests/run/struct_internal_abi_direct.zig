// exit: 0
const S = struct { a: u8, b: u16 };
const O = struct { inner: S, tag: u8 };
noinline fn byteAddress(p: *u8) usize {
    return @intFromPtr(p);
}
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn mutate(p: *S) u8 {
    p.a = 8;
    return 3;
}
noinline fn first(s: S, n: u8) u8 {
    _ = n;
    return s.a;
}
noinline fn isolated(s: S, alias: *S) u16 {
    alias.b = 900;
    var local = s;
    local.b = 333;
    return s.b + local.a;
}
noinline fn swap(s: S, alias: *S) S {
    alias.a = 9;
    return .{ .a = @intCast(s.b), .b = s.a };
}
noinline fn capture(p: *S) S {
    defer p.a = 99;
    return p.*;
}
noinline fn localCapture(x: u8) S {
    var s: S = .{ .a = x, .b = 2 };
    defer s.a = 99;
    return s;
}
noinline fn identity(s: S) S {
    return s;
}
noinline fn nested(o: O) S {
    return o.inner;
}
noinline fn make(x: u8) S {
    return .{ .a = x, .b = 2 };
}
noinline fn combine(a: S, b: S) u16 {
    return a.a * 10 + @as(u16, b.a);
}
noinline fn recur(s: S, n: u8) S {
    if (n == 0) return s;
    var t = s;
    t.a += 1;
    return recur(t, n - 1);
}
noinline fn mixed(x: u8, s: S, y: u16, t: S, z: u8) S {
    return .{ .a = x + s.a + z, .b = s.b + y + t.b };
}
noinline fn under(p: *align(1) const S) S {
    return identity(p.*);
}
noinline fn paramAligned(s: S) bool {
    return @intFromPtr(&s) % @alignOf(S) == 0;
}
pub fn main() u8 {
    var s: S = .{ .a = seed(1), .b = 2 };
    if (first(s, mutate(&s)) != 1 or s.a != 8) return 1;
    s = S{ .a = seed(1), .b = 2 };
    if (isolated(s, &s) != 3 or s.b != 900) return 2;
    s = S{ .a = seed(1), .b = 2 };
    s = swap(s, &s);
    if (s.a != 2 or s.b != 1) return 3;
    s = S{ .a = seed(1), .b = 2 };
    const captured = capture(&s);
    if (captured.a != 1 or captured.b != 2 or s.a != 99) return 4;
    s = S{ .a = seed(1), .b = 2 };
    s = capture(&s);
    if (s.a != 1 or s.b != 2) return 5;
    const local = localCapture(seed(7));
    if (local.a != 7 or local.b != 2) return 6;
    const copied = identity(s);
    s.a = 44;
    if (copied.a != 1 or copied.b != 2) return 7;
    var o: O = .{ .inner = .{ .a = seed(3), .b = 4 }, .tag = 5 };
    const inner = nested(o);
    o.inner.a = 88;
    if (inner.a != 3 or inner.b != 4) return 8;
    if (combine(make(seed(1)), make(seed(2))) != 12) return 9;
    const recursive = recur(.{ .a = seed(2), .b = 9 }, 3);
    if (recursive.a != 5 or recursive.b != 9) return 10;
    const m = mixed(seed(1), .{ .a = 2, .b = 3 }, 4, .{ .a = 5, .b = 6 }, 7);
    if (m.a != 10 or m.b != 13) return 11;
    const u = under(&s);
    s.b = 513;
    if (u.a != 44 or u.b != 2 or !paramAligned(u)) return 12;
    o.inner = identity(o.inner);
    if (o.inner.a != 88 or o.tag != 5) return 13;
    var storage: [@sizeOf(S) + 1]u8 align(16) = undefined;
    const unaligned: *align(1) S = @ptrFromInt(byteAddress(&storage[1]));
    unaligned.a = seed(7);
    unaligned.b = 513;
    const transported = under(unaligned);
    unaligned.b = 999;
    if (transported.a != 7 or transported.b != 513 or !paramAligned(transported)) return 14;
    return 0;
}
