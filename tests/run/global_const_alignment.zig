// exit: 0
// outlined-data: true
const A: u29 align(64) = 32;
const early: *align(128) const u32 align(256) = &later;
const later: u32 align(A * 4) = 123;
const slot: *align(128) const u32 align(512) = &later;
const alias = later;
const inferred align(64) = @as(u16, 7);
const weak: u64 align(1) = 42;
const natural: u32 align(4) = 9;
const opv: u0 align(256) = 0;
const empty: void align(128) = {};
const unused_bad align(0) = 7;
fn seven() u8 { return 7; }
const callback: *const fn () u8 align(256) = seven;
var mutable: u8 = 3;
const pointer: *u8 align(128) = &mutable;
fn read(p: *align(128) const u32) u32 { return p.*; }
fn indirect(p: *align(512) const *align(128) const u32) u32 { return read(p.*); }
fn invoke(p: *align(256) const *const fn () u8) u8 { return p.*(); }
fn writable(p: *align(128) const *u8) void { p.*.* += 1; }
pub fn main() u8 {
    if (read(early) != 123 or indirect(&slot) != 123) return 1;
    const P = *align((&A).*) const u32;
    const p: P = &later;
    if (p.* != 123 or &later != early) return 2;
    const copy: *const u32 = &alias;
    const local_alias = later;
    const local: *const u32 = &local_alias;
    if (copy.* != 123 or local.* != 123 or inferred != 7) return 3;
    const w: *align(1) const u64 = &weak;
    const n: *align(4) const u32 = &natural;
    if (w.* != 42 or n.* != 9 or invoke(&callback) != 7) return 4;
    writable(&pointer);
    if (mutable != 4) return 5;
    _ = &opv; _ = &empty;
    return 0;
}
