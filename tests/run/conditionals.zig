// exit: 0
fn choose(c: bool, a: u8, b: u8) u8 { return if (c) a else b; }
fn nested(x: u8) u8 { return if (x < 10) (if (x == 4) 31 else 32) else if (x > 20) 33 else 34; }
fn both(c: bool) u8 { if (c) { return 41; } else { return 42; } }
fn one(c: bool) u8 { const x = if (c) 51 else return 52; return x; }
fn other(c: bool) u8 { const x = if (c) return 62 else 61; return x; }
fn cast(c: bool, x: u16) u8 { return if (c) @intCast(x) else @truncate(x); }
fn store(c: bool, x: u16) u8 {
    var y: u8 = 0;
    y = if (c) @intCast(x) else @truncate(x);
    return y;
}
fn identity(x: u8) u8 { return x; }
fn argument(c: bool, x: u16) u8 { return identity(if (c) @intCast(x) else @truncate(x)); }
fn inferred(c: bool) u16 { const x = if (c) @as(u16, 10) else 11; return x; }
fn zero(c: bool, x: u0) u0 { return if (c) x else 0; }
fn peer(c: bool, x: u8, y: u16) u16 { const z = if (c) x else y; return z; }
fn boolean(c: bool, x: bool) bool { return if (c) x else !x; }
fn effect(c: bool) u8 {
    var x: u8 = 1;
    if (c) x = 7 else x = 8;
    if (c) { const y: u8 = 2; x = x + y; }
    if (!c) { x = x + 3; }
    return x;
}
fn guarded(c: bool, x: u8) u8 { return if (c) 71 else @divTrunc(100, x); }
fn void_arms(c: bool) void { if (c) {} else {} }
const selected = if (true) 17 else @divTrunc(1, 0);
const selected_type = if (false) i8 else u8;
pub fn main() u8 {
    if (choose(true, 2, 3) != 2 or choose(false, 2, 3) != 3) return 1;
    if (nested(4) != 31 or nested(5) != 32 or nested(21) != 33 or nested(15) != 34) return 2;
    if (both(true) != 41 or both(false) != 42) return 3;
    if (one(true) != 51 or one(false) != 52 or other(true) != 62 or other(false) != 61) return 4;
    if (cast(true, 255) != 255 or cast(false, 256) != 0) return 5;
    if (peer(true, 7, 300) != 7 or peer(false, 7, 300) != 300) return 6;
    if (!boolean(true, true) or boolean(false, true)) return 7;
    if (effect(true) != 9 or effect(false) != 11) return 8;
    if (guarded(true, 0) != 71 or guarded(false, 2) != 50) return 9;
    void_arms(true);
    void_arms(false);
    const typed: selected_type = selected;
    if (typed != 17) return 10;
    if (store(true, 255) != 255 or store(false, 256) != 0) return 11;
    if (argument(true, 255) != 255 or argument(false, 256) != 0) return 12;
    if (inferred(true) != 10 or inferred(false) != 11) return 13;
    if (zero(true, 0) != 0 or zero(false, 0) != 0) return 14;
    return 0;
}
