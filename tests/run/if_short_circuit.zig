// exit: 0
fn division(c: bool, x: u8) bool { return c and (if (x == 0) false else @divTrunc(100, x) == 50); }
fn either(c: bool, x: u8) bool { return c or (if (x == 0) true else @divTrunc(100, x) == 25); }
fn early(c: bool, d: bool) u8 {
    const x = if (c and d) 17 else return 18;
    return x;
}
fn known_return(c: bool) u8 {
    const x = true and (if (c) return 31 else return 32);
    return @intFromBool(x);
}
fn runtime_return(c: bool, d: bool) u8 {
    const x = c or (if (d) return 41 else return 42);
    return @intFromBool(x);
}
fn signed(x: i5, y: i5) u8 { return if (x < y) 1 else if (x == y) 2 else 3; }
fn wide(x: u64, y: u64) u8 { return if (x >= y) 4 else 5; }
pub fn main() u8 {
    if (division(false, 0) or division(true, 0) or !division(true, 2)) return 1;
    if (!either(true, 0) or !either(false, 0) or !either(false, 4) or either(false, 2)) return 2;
    if (early(true, true) != 17 or early(false, true) != 18 or early(true, false) != 18) return 3;
    if (signed(-16, 15) != 1 or signed(-1, -1) != 2 or signed(15, -16) != 3) return 4;
    if (wide(0xffffffffffffffff, 1) != 4 or wide(1, 0xffffffffffffffff) != 5) return 5;
    if (known_return(true) != 31 or known_return(false) != 32) return 6;
    if (runtime_return(true, true) != 1 or runtime_return(false, true) != 41 or runtime_return(false, false) != 42) return 7;
    return 0;
}
