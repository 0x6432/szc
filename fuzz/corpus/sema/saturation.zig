// exit: 0
fn add(a: u8, b: u8) u8 {
    return a +| b;
}
fn sub(a: u64, b: u64) u64 {
    return a -| b;
}
fn mul(a: i64, b: i64) i64 {
    return a *| b;
}
fn signed_add(a: i5, b: i5) i5 {
    return a +| b;
}
fn signed_sub(a: i64, b: i64) i64 {
    return a -| b;
}
fn shift(a: i64, b: u64) i64 {
    return a <<| b;
}
fn unsigned_shift(a: u5, b: u64) u5 {
    return a <<| b;
}
fn constant_shift(a: i5) i5 {
    return a <<| 100000000000000000000;
}
pub fn main() u8 {
    const max: i64 = 0x7fff_ffff_ffff_ffff;
    const min: i64 = -0x8000_0000_0000_0000;
    const huge: u64 = 0xffff_ffff_ffff_ffff;
    const c: i5 = @as(i5, -1) <<| 100000000000000000000;
    const ok = add(250, 10) == 255 and add(1, 2) == 3 and sub(0, huge) == 0 and sub(huge, 1) == huge - 1 and
        mul(min, -1) == max and mul(min, 2) == min and mul(min, 0) == 0 and mul(2, -3) == -6 and
        signed_add(15, 1) == 15 and signed_add(-16, -1) == -16 and signed_sub(min, 1) == min and signed_sub(max, -1) == max and
        shift(1, 63) == max and shift(-1, 63) == min and shift(min, 0) == min and shift(-1, huge) == min and shift(0, huge) == 0 and
        unsigned_shift(1, huge) == 31 and unsigned_shift(0, huge) == 0 and unsigned_shift(31, 1) == 31 and unsigned_shift(3, 1) == 6 and
        constant_shift(-1) == -16 and c == 15;
    return @intFromBool(!ok);
}
