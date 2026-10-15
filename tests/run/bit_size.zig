// exit: 0
const bits: u16 = @bitSizeOf([2][3]u7);
const padded: u16 = @sizeOf([2][3]u7)*8;
pub fn main() u8 {
    if (bits != 42 or padded != 48) return 1;
    if (@bitSizeOf(u257) != 257 or @sizeOf(u257)*8 != 384) return 2;
    if (@bitSizeOf([3]u257) != 771 or @bitSizeOf(bool) != 1) return 3;
    if (@bitSizeOf(void) != 0 or @bitSizeOf([18446744073709551615]u0) != 0) return 4;
    if (@bitSizeOf(*type) != 64 or @bitSizeOf(*const fn () void) != 64) return 5;
    if (@bitSizeOf(f32) != 32 or @bitSizeOf(f64) != 64) return 6;
    return 0;
}
