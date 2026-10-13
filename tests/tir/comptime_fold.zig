const a = 1 << 70;
const b = a >> 68;
const c = (a - 1) & 0xff;
const d0: u8 = 0;
const d = ~d0;
const e = -5 >> 1;
const f = 7 % 3 + 100 / 7;
const g: i8 = -128;
const h0: u8 = 250;
const h = h0 +% 10;
const i: i8 = -%g;
const big = 0x1_0000_0000_0000_0000 * 3 - 1;
pub fn main() u8 {
    var x: u8 = 0;
    x = x ^ (b | 1);
    var y: i64 = e;
    y = y * f + g + i;
    return x + c - c + h - 4 + d - 255;
}
