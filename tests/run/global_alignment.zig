// exit: 0
// outlined-data: true
const A: u29 = 64;
var early: *align(64) u32 align(128) = &later;
var later: u32 align(A) = if (&later == &later) 7 else 0;
var cycle: *align(128) u8 align(256) = &cycle_value;
var cycle_value: u8 align(128) = if (&cycle == &cycle) 11 else 0;
var weak: u64 align(1) = 42;
var natural: u32 align(4) = 1;
var inferred align(4096) = @as(u16, 3);
var zero: u64 align(256) = 0;
var undef: u32 align(128) = undefined;
var opv: u0 align(512) = 0;
var empty: void align(128) = {};
fn seven() u8 { return 7; }
fn nine() u8 { return 9; }
var callback: *const fn () u8 align(256) = seven;
// This invalid attribute remains lazy until the declaration is referenced.
var unused: u8 align(0) = 0;
fn read(p: *align(64) const u32) u32 { return p.*; }
fn increment() void { defer later += 1; early.* += 2; }
pub fn main() u8 {
    if (read(early) != 7 or cycle.* != 11 or zero != 0) return 1;
    increment();
    if (later != 10 or early != &later) return 2;
    cycle.* = 13;
    const p: *align(1) u64 = &weak;
    p.* += 1;
    const q: *align(4) u32 = &natural;
    q.* += 1;
    inferred += 2; zero = 9; undef = 12;
    if (cycle_value != 13 or weak != 43 or natural != 2 or inferred != 5 or zero != 9 or undef != 12) return 3;
    const slot: *align(256) *const fn () u8 = &callback;
    slot.* = nine;
    if (callback() != 9) return 4;
    opv = 0; empty = {};
    _ = &opv; _ = &empty;
    return 0;
}
