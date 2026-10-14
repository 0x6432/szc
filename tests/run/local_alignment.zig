// exit: 0
const alignment: u29 = 32 * 2;
fn read(p: *align(64) const u32) u32 { return p.*; }
fn recurse(parent: *align(64) const u32, depth: u8) u32 {
    var local: u32 align(64) = parent.* + 1;
    local += 0;
    if (&local == parent) return 0;
    if (depth == 0) return local;
    const child = recurse(&local, depth - 1);
    if (local != parent.* + 1) return 0;
    return local + child;
}
fn seven() u8 { return 7; }
fn nine() u8 { return 9; }
fn cleanup(p: *align(64) u32) u32 { defer p.* += 1; p.* += 2; return p.*; }
pub fn main() u8 {
    var n: u32 align(alignment) = 5;
    n += 0;
    if (read(&n) != 5 or recurse(&n, 3) != 30) return 1;
    if (cleanup(&n) != 7 or n != 8) return 2;
    var weak: u64 align(1) = 9;
    const p: *align(1) u64 = &weak;
    p.* += 1;
    if (weak != 10) return 3;
    var natural: u32 align(4) = 2;
    natural += 1;
    const explicit: *align(4) u32 = &natural;
    if (explicit.* != 3) return 4;
    var slot: *align(64) u32 align(128) = &n;
    const address: *align(128) *align(64) u32 = &slot;
    address.* = &n;
    address.*.* += 1;
    if (n != 9) return 5;
    var callback: *const fn () u8 align(256) = seven;
    const callback_address: *align(256) *const fn () u8 = &callback;
    callback_address.* = nine;
    if (callback() != 9) return 6;
    var sum: u32 = 0;
    for (0..3) |i| {
        var value: u32 align(4096) = @intCast(i);
        value += 2;
        sum += value;
    }
    if (sum != 9) return 7;
    var zero: u0 align(64) = 0;
    zero = 0;
    var empty: void align(128) = {};
    empty = {};
    _ = &zero;
    _ = &empty;
    return 0;
}
