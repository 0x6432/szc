// exit: 0
const ints: [3]i7 align(64) = .{-63, 3, 7};
const wide = [_]u33{4294967297, 8589934591, 3};
const flags = [_]bool{true, false};
const numbers = [_]u8{13, 29};
fn read(p: *const [3]i7, n: usize) i7 { return p[n]; }
fn known(p: *const [3]i7) i7 { return p[2]; }
fn read_wide(p: *const [3]u33, n: usize) u33 { return p[n]; }
fn read_bool(p: *const [2]bool, n: usize) bool { return p[n]; }
fn read_ref(p: *const [2]*const u8, n: usize) u8 { return p[n].*; }
fn read_callback(p: *const [2]*const fn () u8, n: usize) u8 { return p[n](); }
fn target() u8 { return 17; }
fn other() u8 { return 19; }
fn bytes(n: usize) u8 { const a = [_]u8{7,9}; return a[n]; }
fn str(n: usize) u8 { return "hi"[n]; }
fn plain_str(p: *const [2]u8, n: usize) u8 { return p[n]; }
fn volatile_read(p: *const volatile [2]u8, n: usize) u8 { return p[n]; }
fn zero(p: *const [2]u0, n: usize) u0 { return p[n]; }
fn no_bits(p: *const [2]void, n: usize) void { return p[n]; }
fn zero_value(n: usize) u0 { const a = [_]u0{0, 0}; return a[n]; }
fn void_value(n: usize) void { const a = [_]void{{}, {}}; return a[n]; }
fn undefined_value(n: usize) u8 { const a = [_]u8{undefined, undefined}; return a[n]; }
pub fn main() u8 {
    if (read(&ints, 0) != -63 or known(&ints) != 7) return 1;
    if (read_wide(&wide, 1) != 8589934591) return 2;
    if (!read_bool(&flags, 0) or read_bool(&flags, 1)) return 3;
    const x: u8 = 31;
    const y: u8 = 47;
    const pointers: [2]*const u8 align(64) = .{&x, &y};
    if (read_ref(&pointers, 1) != 47) return 4;
    const callbacks: [2]*const fn () u8 align(64) = .{target, other};
    if (read_callback(&callbacks, 1) != 19) return 5;
    if (bytes(1) != 9 or str(2) != 0 or plain_str("hi", 1) != 105) return 6;
    if (volatile_read(&numbers, 1) != 29) return 7;
    const z = [_]u0{0, 0};
    if (zero(&z, 1) != 0) return 8;
    const v = [_]void{{}, {}};
    no_bits(&v, 1);
    if (zero_value(2) != 0) return 9;
    void_value(2);
    _ = undefined_value(2);
    return 0;
}
