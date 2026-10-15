// exit: 0
fn make(n: u8) [3]u8 { return .{n, 2, 3}; }
fn sum(a: [3]u8) u8 { return a[0] + a[1] + a[2]; }
fn copy(a: [3]u8) [3]u8 { return a; }
fn alias(a: [3]u8, p: *[3]u8) u8 { p[0] = 29; return a[0]; }
fn change(p: *[3]u8) u8 { p[0] = 31; return 0; }
fn later(a: [3]u8, n: u8) u8 { _ = n; return a[0]; }
fn recurse(a: [3]u8, n: u8) [3]u8 {
    if (n == 0) return a;
    var b = a;
    b[0] += 1;
    return recurse(b, n - 1);
}
fn parameter_alignment(a: [3]u64) u64 {
    const p: *const [3]u64 = &a;
    return p[1];
}
fn nested(a: [2][3]u8) [2][3]u8 {
    var b = a;
    b[1][1] += 1;
    return b;
}
fn empty(a: [0]u64) [0]u64 { return a; }
fn zeros(a: [3]u0) [3]u0 { return a; }
fn test_calls(n: u8) u8 {
    var a = make(n);
    const b = copy(a);
    a[0] = n + 1;
    if (b[0] != n or sum(make(7)) != 12) return 1;
    if (alias(a, &a) != n + 1 or a[0] != 29) return 2;
    if (later(a, change(&a)) != 29 or a[0] != 31) return 3;
    if (recurse(make(n), 3)[0] != n + 3) return 4;
    const underaligned: [3]u64 align(1) = .{1, 4294967297, 3};
    if (parameter_alignment(underaligned) != 4294967297) return 5;
    const c = nested(.{make(n), make(n + 1)});
    if (c[1][0] != n + 1 or c[1][1] != 3) return 6;
    if (empty(.{}).len != 0 or zeros(.{0,0,0})[1] != 0) return 7;
    return 0;
}
pub fn main() u8 { return test_calls(7); }
