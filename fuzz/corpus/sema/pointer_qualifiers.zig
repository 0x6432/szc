fn read(p: *const *const u8) u8 { return p.*.*; }
pub fn main() u8 {
    var n: u8 = 1;
    n += 0;
    var p: *u8 = &n;
    p = &n;
    return read(&p);
}
