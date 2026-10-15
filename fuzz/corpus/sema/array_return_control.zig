// exit: 0
fn select(c: bool, n: u8) [2]u8 { return if (c) .{n, 2} else .{3, n}; }
fn labeled(c: bool, n: u8) [2]u8 {
    return result: { if (c) break :result .{n, 2}; break :result .{3, n}; };
}
fn loop(c: bool, n: u8) [2]u8 {
    return while (c) { break .{n, 2}; } else .{3, n};
}
fn deferred(n: u8) [2]u8 {
    var a = [_]u8{n, 2};
    defer a[0] = 41;
    return a;
}
fn break_defer(n: u8) [2]u8 {
    var a = [_]u8{n, 2};
    return block: { defer a[0] = 41; break :block a; };
}
fn partial(c: bool, n: u8) [2]u8 {
    const a: [2]u8 = if (c) return .{n, 2} else .{3, n};
    return a;
}
fn exercise(c: bool, n: u8) u8 {
    const a = select(c, n);
    const b = labeled(c, n);
    const d = loop(c, n);
    if (a[0] != b[0] or a[1] != b[1] or a[0] != d[0] or a[1] != d[1]) return 1;
    if (deferred(n)[0] != n or break_defer(n)[0] != n) return 2;
    const p = partial(c, n);
    if (p[0] != a[0] or p[1] != a[1]) return 3;
    return 0;
}
pub fn main() u8 { if (exercise(true, 7) != 0) return 1; return exercise(false, 7); }
