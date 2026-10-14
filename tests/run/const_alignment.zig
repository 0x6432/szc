// exit: 0
// outlined-data: true
fn read(p: *align(128) const u32) u32 { return p.*; }
fn indirect(p: *align(256) const *align(128) const u32) u32 { return read(p.*); }
fn known() *align(128) const u32 { const n: u32 align(128) = 123; return &n; }
fn runtime(n: u32, choose: bool) u32 {
    const value: u32 align(64) = n + 1;
    const p: *align(64) const u32 = if (choose) &value else &value;
    const q = &value;
    if (p != q) return 0;
    const alias = value;
    const natural: *const u32 = &alias;
    return p.* + natural.*;
}
fn recurse(parent: *align(64) const u32, depth: u8) u32 {
    const local: u32 align(64) = parent.* + 1;
    if (&local == parent) return 0;
    if (depth == 0) return local;
    const child = recurse(&local, depth - 1);
    if (local != parent.* + 1) return 0;
    return local + child;
}
fn seven() u8 { return 7; }
pub fn main() u8 {
    const n: u32 align(128) = 123;
    const p: *align(128) const u32 align(256) = &n;
    if (read(&n) != 123 or indirect(&p) != 123 or read(known()) != 123) return 1;
    if (runtime(7, true) != 16 or runtime(9, false) != 20) return 2;
    const start: u32 align(64) = 5;
    if (recurse(&start, 3) != 30) return 3;
    const a align(64) = 32;
    const inferred = (&a).*;
    const P = *align(inferred) const u32;
    const q: P = &n;
    if (q.* != 123) return 4;
    const T align(16) = u8;
    const copy: (&T).* = 7;
    const f align(64) = seven;
    if (f() != copy) return 5;
    const callback: *const fn () u8 align(256) = seven;
    const slot: *align(256) const *const fn () u8 = &callback;
    if (slot.*() != 7) return 6;
    const weak: u64 align(1) = 42;
    const w: *align(1) const u64 = &weak;
    if (w.* != 42) return 7;
    var sum: u32 = 0;
    for (0..3) |i| {
        const element: u32 align(128) = @intCast(i);
        const address: *align(128) const u32 = &element;
        sum += address.*;
    }
    if (sum != 3) return 8;
    return 0;
}
