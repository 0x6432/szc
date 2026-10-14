// exit: 0
const a: u8 = 7;
const b: u8 = 7;
fn sum(n: u16) u16 {
    const p = &n;
    if (n == 0) return p.*;
    const rest = sum(n - 1);
    return p.* + rest;
}
fn branch(n: u16, c: bool) u16 {
    const x = n + 1;
    if (c) _ = &x;
    return (&x).*;
}
fn loop(n: usize) u8 {
    for (0..n) |i| {
        const p = &i;
        const q = &p;
        if (q.*.* != i or q.* != p) return 1;
    }
    return 0;
}
fn cleanup(n: u16, out: *u16) void {
    const x = n + 2;
    defer out.* = (&x).*;
    if (n == 0) return;
}
pub fn main() u8 {
    const c: u8 = 7;
    if (&a == &b or &a == &c or &a != &a) return 1;
    if (sum(10) != 55) return 2;
    if (branch(11, true) != 12 or branch(13, false) != 14) return 3;
    if (loop(4) != 0) return 4;
    var result: u16 = 0;
    cleanup(0, &result);
    if (result != 2) return 5;
    cleanup(5, &result);
    if (result != 7) return 6;
    return 0;
}
