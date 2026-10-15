// exit: 0
const wide_size = @sizeOf(u257);
const wide_align = @alignOf(u257);
const a: u8 = @sizeOf([2][3]i33);
const b: u8 = @alignOf([0]u16);
const zero = @sizeOf([18446744073709551615]u0);
const logical_empty = @sizeOf([0]type);
const logical_align = @alignOf([2]type);
const fn_align = @alignOf(fn () void);
pub fn main() u8 {
    if (wide_size != 48 or wide_align != 16 or a != 48 or b != 2) return 1;
    if (zero != 0 or logical_empty != 0 or logical_align != 1 or fn_align != 1) return 2;
    if (@sizeOf(u513) != 80 or @sizeOf(u65535) != 8192 or @alignOf(u65535) != 16) return 3;
    if (@sizeOf([*]align(1) u8) != 8 or @alignOf(*align(64) u8) != 8) return 4;
    if (@sizeOf(bool) != 1 or @sizeOf(void) != 0 or @alignOf(void) != 1) return 5;
    if (@sizeOf(f32) != 4 or @alignOf(f64) != 8) return 6;
    return 0;
}
