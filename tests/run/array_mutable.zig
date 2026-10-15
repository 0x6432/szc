// exit: 0
fn copy(dst: *[3]u16, src: *const [3]u16) void { dst.* = src.*; }
fn set(p: *u16, n: u16) void { p.* = n; }
fn sample(p: *const [3]u16, i: usize) u16 { return p[i]; }
fn exercise(n: u16) u8 {
    var a = [_]u16{n, 2, 3};
    const b = a;
    const c = b;
    const bp = &b;
    const cp = &c;
    if (bp[0] != n or cp[0] != n) return 1;
    a[0] += 4;
    set(&a[1], 12);
    if (b[0] != n or b[1] != 2 or c[1] != 2) return 2;
    var d: [3]u16 align(256) = undefined;
    copy(&d, &a);
    copy(&d, &d);
    if (sample(&d, 0) != n + 4 or d[1] != 12 or d[2] != 3) return 3;
    d = b;
    if (d[0] != n or d[1] != 2) return 4;
    const e align(128) = a;
    const ep: *align(128) const [3]u16 = &e;
    if (ep[1] != 12) return 5;
    var nested: [2][3]u16 = .{a, d};
    const old = nested;
    nested[1][2] = 29;
    const row = nested[1];
    nested[1][2] = 31;
    if (row[2] != 29 or old[1][2] != 3 or nested[1][2] != 31) return 6;
    const p = &nested;
    const pp = &p;
    pp.*[0][0] = 17;
    if (nested[0][0] != 17) return 7;
    var flags = [_]bool{true, false};
    const flags_old = flags;
    flags[1] = true;
    if (flags_old[1] or !flags[1]) return 8;
    var zeros: [3]u0 = undefined;
    zeros[1] = 0;
    const zeros_copy = zeros;
    if (zeros_copy[2] != 0) return 9;
    var empty: [0]u8 = .{};
    const empty_copy = empty;
    empty = empty_copy;
    if (empty.len != 0) return 10;
    return 0;
}
pub fn main() u8 { return exercise(7); }
