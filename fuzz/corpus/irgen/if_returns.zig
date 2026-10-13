// exit: 0
fn both(c: bool) u8 { return if (c) return 7 else return 8; }
fn nested(c: bool, d: bool) u8 {
    if (c) { if (d) return 11 else return 12; } else { return 13; }
    return 99;
}
fn arm(c: bool, x: u8) u8 { return if (c) x else unreachable; }
fn known() u8 { if (true) return 21; return 22; }
fn void_return(c: bool) void { if (c) return else return; }
pub fn main() u8 {
    if (both(true) != 7 or both(false) != 8) return 1;
    if (nested(true, true) != 11 or nested(true, false) != 12 or nested(false, false) != 13) return 2;
    if (arm(true, 19) != 19 or known() != 21) return 3;
    void_return(true);
    void_return(false);
    return 0;
}
