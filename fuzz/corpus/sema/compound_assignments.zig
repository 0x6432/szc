// exit: 0
fn operations(a: u8, b: u16, shift: u8) u8 {
    var x: u8 = a;
    x += @intCast(b);
    if (x != 7) return 1;
    x *= 2;
    x -= 4;
    x /= 2;
    x %= 3;
    if (x != 2) return 2;
    x <<= @intCast(shift);
    x >>= 1;
    x |= 3;
    x &= 6;
    x ^= 7;
    if (x != 1) return 3;
    x +%= 255;
    x -%= 1;
    x *%= 2;
    if (x != 254) return 4;
    x +|= 10;
    x -|= 254;
    x *|= 255;
    x <<|= 64;
    if (x != 255) return 5;
    return 0;
}
pub fn main() u8 { return operations(3, 4, 2); }
