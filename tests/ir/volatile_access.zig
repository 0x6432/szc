const known: u29 = 16;
const known_pointer: *const volatile u29 = &known;
const A: u29 = known_pointer.*;
var global: u8 align(A) = 3;
fn seven() u8 { return 7; }
fn nine() u8 { return 9; }
var callback: *const fn () u8 = seven;
fn adjust(p: *volatile u8) u8 {
    defer p.* += 1;
    p.* += 2;
    for (0..3) |_| { p.* += 1; }
    _ = p.*;
    return p.*;
}
fn choose(flag: bool, a: *volatile u8, b: *u8) *volatile u8 { return if (flag) a else b; }
fn swap(slot: *volatile *const fn () u8) u8 { slot.* = nine; _ = slot.*; return slot.*(); }
pub fn main() u8 {
    const p: *volatile u8 = &global;
    if (adjust(p) != 8 or global != 9) return 1;
    var local: u8 align(1) = 4;
    const q = choose(false, p, &local);
    q.* = 5;
    if (q.* != 5 or local != 5 or swap(&callback) != 9) return 2;
    const snapshot: *const volatile u8 = p;
    if (snapshot.* != 9 or &p.* != p) return 3;
    const zero: *volatile u0 = undefined;
    const empty: *volatile void = undefined;
    _ = zero.*; zero.* = 0; _ = empty.*; empty.* = {};
    q.* = undefined;
    const P = *align(A) u8;
    const aligned: P = &global;
    _ = aligned;
    return 0;
}
