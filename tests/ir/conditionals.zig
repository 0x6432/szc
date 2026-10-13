fn choose(c: bool, a: u8, b: u16) u16 { const x = if (c) a else b; return x; }
fn early(c: bool) u8 { const x = if (c) 7 else return 8; return x; }
fn all(c: bool) u8 { if (c) return 9 else return 10; }
pub fn main() u8 {
    var x: u8 = 0;
    if (choose(true, 3, 4) == 3) x = early(false) else x = all(true);
    return if (false) @divTrunc(1, 0) else x;
}
