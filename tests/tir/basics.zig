const big = 0x1_0000_0000_0000_0000;
const K: u8 = 200;
fn add(a: u32, b: u32) u32 {
    var c: u32 = a + b * 2;
    c = c - K;
    return c;
}
fn check(x: u8, y: bool) bool {
    return x < 300 and (y or x == K);
}
pub fn main() u8 {
    const r = add(1, 2);
    _ = check(3, true);
    const s: u64 = r;
    _ = s << 3;
    return 7 + 1;
}
