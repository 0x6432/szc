// exit: 0
fn zero(p: *u0) u0 { p.* = 0; return p.*; }
fn empty(p: *void) void { p.* = {}; }
pub fn main() u8 {
    var n: u0 = 0;
    var v: void = {};
    empty(&v);
    const p: *void = undefined;
    p.* = {};
    _ = p.*;
    const q: *u0 = undefined;
    q.* = 0;
    const raw: *u8 = undefined;
    const readonly: *const u8 = raw;
    _ = readonly;
    return zero(&n) + q.*;
}
