// exit: 0
// Typed dynamic roots and round-tripped actual addresses; never dereference a fake address.
const I = struct { x: u32, tag: u8 };
const S = struct { tiny: u8, wide: u64, inner: I, n: u9, flag: bool };
noinline fn seed(x: u8) u8 {
    return x;
}
noinline fn root(p: *S) *S {
    return p;
}
noinline fn address(p: *S) usize {
    return @intFromPtr(p);
}
noinline fn read(p: *const volatile u64) u64 {
    return p.*;
}
noinline fn change(p: *S) u32 {
    p.inner.x += 10;
    return p.inner.x;
}
noinline fn addr(p: *const anyopaque) usize {
    return @intFromPtr(p);
}
pub fn main() u8 {
    var s: S align(64) = .{ .tiny = seed(1), .wide = 19, .inner = .{ .x = 31, .tag = 2 }, .n = 257, .flag = true };
    const p = root(&s);
    p.tiny = 7;
    @field(p, "wide") = 0x1122334455667788;
    if (s.tiny != 7 or read(&p.wide) != 0x1122334455667788) return 1;
    if (change(p) != 41 or s.inner.x != 41) return 2;
    const q: *S = @ptrFromInt(address(&s));
    q.inner.tag = 9;
    if (s.inner.tag != 9 or addr(@ptrCast(&q.inner.x)) - address(&s) != @offsetOf(S, "inner") + @offsetOf(I, "x")) return 3;
    const under: *allowzero align(1) volatile S = p;
    const w: *allowzero align(1) volatile u64 = &under.wide;
    w.* = 513;
    if (@TypeOf(&under.wide) != *allowzero align(1) volatile u64 or under.wide != 513) return 4;
    const cp: *const S = p;
    if (@TypeOf(&cp.inner.x) != *const u32 or cp.inner.x != 41) return 5;
    p.n += 1;
    p.flag = false;
    if (s.n != 258 or s.flag) return 6;
    if (address(&s) % 64 != 0 or @TypeOf(&s.wide) != *u64) return 7;
    const old = p.*;
    p.wide = 1;
    if (old.wide != 513 or s.wide != 1) return 8;
    const sub = p.inner;
    p.inner.x = 99;
    if (sub.x != 41 or s.inner.x != 99) return 9;
    return 0;
}
