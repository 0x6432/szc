// exit: 0
fn sum(n: u8) u8 {
    var i: u8 = 0;
    var total: u8 = 0;
    while (i < n) : (i += 1) {
        if (i == 2) continue;
        if (i == 5) break;
        total += i;
    }
    return total;
}
fn normal(n: u8) u8 {
    var i: u8 = 0;
    var total: u8 = 0;
    while (i < n) { total += i; i += 1; } else { total += 10; }
    return total;
}
fn no_else(n: u8) u8 { var i: u8 = 0; while (i < n) : (i += 1) {} return i; }
fn unbraced(n: u8) u8 { var i: u8 = 0; while (i < n) i += 1; return i; }
fn never_entered() u8 { while (false) { _ = @divTrunc(1, 0); } else { return 21; } }
fn once() u8 { while (true) return 22; }
fn zero(n: u8) u0 { var i: u8 = 0; return while (i < n) : (i += 1) { if (i == 2) break 0; } else 0; }
fn guarded(n: u8, x: u8) u8 { while (n > 0) return @divTrunc(100, x); return 17; }
pub fn main() u8 {
    if (sum(8) != 8 or sum(3) != 1 or sum(0) != 0) return 1;
    if (normal(0) != 10 or normal(4) != 16) return 2;
    if (no_else(3) != 3 or unbraced(4) != 4) return 3;
    if (never_entered() != 21 or once() != 22) return 4;
    if (zero(0) != 0 or zero(3) != 0) return 5;
    if (guarded(0, 0) != 17 or guarded(1, 2) != 50) return 6;
    return 0;
}
