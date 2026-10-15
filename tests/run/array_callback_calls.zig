// exit: 0
fn make(n: u8) [2]u8 { return .{n, 2}; }
fn other(n: u8) [2]u8 { return .{n, 3}; }
fn transform(a: [2]u8) [2]u8 { return .{a[1], a[0]}; }
fn invoke(f: *const fn (u8) [2]u8, n: u8) [2]u8 { return f(n); }
fn consume(f: *const fn ([2]u8) [2]u8, a: [2]u8) [2]u8 { return f(a); }
fn many(a: u8, b: u8, c: [2]u8, d: u8, e: u8, f: u8, g: [2]u8, h: u8) [3]u8 {
    return .{a + b + c[0], d + e + f, g[1] + h};
}
fn test_callbacks(c: bool, n: u8) u8 {
    const f = if (c) &make else &other;
    const a = invoke(f, n);
    const b = consume(&transform, a);
    if (b[1] != n or b[0] != (if (c) @as(u8, 2) else @as(u8, 3))) return 1;
    const callbacks = [_]*const fn (u8) [2]u8{&make, &other};
    if (invoke(callbacks[0], n)[0] != n or invoke(callbacks[1], n)[1] != 3) return 2;
    const out = many(1, 2, .{3,4}, 4, 5, 6, .{7,8}, 9);
    if (out[0] != 6 or out[1] != 15 or out[2] != 17) return 3;
    return 0;
}
pub fn main() u8 { if (test_callbacks(true, 7) != 0) return 1; return test_callbacks(false, 7); }
