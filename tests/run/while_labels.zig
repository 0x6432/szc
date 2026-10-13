// exit: 0
fn nested() u8 {
    var i: u8 = 0;
    var total: u8 = 0;
    outer: while (i < 4) : (i += 1) {
        var j: u8 = 0;
        while (j < 3) : (j += 1) {
            if (i == 0 and j == 1) continue :outer;
            if (i == 2) break :outer;
            total += 1;
        }
    }
    return total;
}
fn else_outer(c: bool) u8 {
    var i: u8 = 0;
    var total: u8 = 0;
    while (i < 3) : (i += 1) {
        while (false) {} else { if (c) continue; break; }
        total += 100;
    }
    return total + i;
}
fn named_else() u8 { return target: while (false) {} else { break :target 17; }; }
fn continue_exit() u8 { var i: u8 = 0; while (i < 3) : (return 19) { i += 1; continue; } return 20; }
fn scoped(n: u8) u8 {
    var i: u8 = 0;
    var total: u8 = 0;
    while (i < n) : (i += 1) { var local: u8 = i; local += 1; total += local; }
    return total;
}
pub fn main() u8 {
    if (nested() != 4) return 1;
    if (else_outer(true) != 3 or else_outer(false) != 0) return 2;
    if (named_else() != 17 or continue_exit() != 19) return 3;
    if (scoped(4) != 10) return 4;
    return 0;
}
