// exit: 0
fn choose(c: bool) u8 { return while (c) { break 7; } else 8; }
fn early(c: bool) u8 { const x = while (c) { break 9; } else return 10; return x; }
fn peer(c: bool, a: u8, b: u16) u16 { const x = while (c) { break a; } else b; return x; }
fn multiple(n: u8, a: u8, b: u16) u16 {
    var i: u8 = 0;
    const x = while (i < n) : (i += 1) { if (i == 1) break a; if (i == 2) break b; } else b;
    return x;
}
fn typed(c: bool, x: u16) u8 { return while (c) { break @intCast(x); } else @truncate(x); }
fn count(n: u8) u8 { var i: u8 = 0; return while (true) : (i += 1) { if (i == n) break i; }; }
fn continue_break() u8 { var i: u8 = 0; return while (i < 2) : (break 31) { i += 1; } else 32; }
pub fn main() u8 {
    if (choose(true) != 7 or choose(false) != 8) return 1;
    if (early(true) != 9 or early(false) != 10) return 2;
    if (peer(true, 3, 300) != 3 or peer(false, 3, 300) != 300) return 3;
    if (multiple(3, 4, 500) != 4 or multiple(0, 4, 500) != 500) return 4;
    if (typed(true, 255) != 255 or typed(false, 256) != 0) return 5;
    if (count(5) != 5) return 6;
    if (continue_break() != 31) return 7;
    return 0;
}
