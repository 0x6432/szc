// exit: 0
fn add(a: i64, b: i64) i64 {
    return a + b;
}
fn sub(a: i64, b: i64) i64 {
    return a - b;
}
fn mul(a: i64, b: i64) i64 {
    return a * b;
}
fn unsigned_mul(a: u64, b: u64) u64 {
    return a * b;
}
fn small_mul(a: i5, b: i5) i5 {
    return a * b;
}
fn negate(a: i64) i64 {
    return -a;
}
fn signed_cast(a: i64) i5 {
    return @intCast(a);
}
fn unsigned_cast(a: u64) u5 {
    return @intCast(a);
}
fn negative_cast(a: i64) u64 {
    return @intCast(a);
}
fn positive_cast(a: u64) i64 {
    return @intCast(a);
}
fn zero_cast(a: u8) u0 {
    return @intCast(a);
}
fn zero_shift(a: u0, b: u0) u0 {
    return a << b;
}
fn divide(a: u64, b: u64) u64 {
    return a / b;
}
fn remainder(a: u64, b: u64) u64 {
    return a % b;
}
pub fn main() u8 {
    _ = zero_cast(0);
    _ = zero_shift(0, 0);
    const max: i64 = 0x7fff_ffff_ffff_ffff;
    const min: i64 = -0x8000_0000_0000_0000;
    const unsigned_max: u64 = 0xffff_ffff_ffff_ffff;
    const shifted: u5 = @as(u5, 31) << 4;
    const signed_shifted: i5 = @as(i5, -1) << 4;
    const ok = add(max, 0) == max and add(min, max) == -1 and sub(min, min) == 0 and
        mul(min, 1) == min and mul(min, 0) == 0 and mul(max, 1) == max and mul(-2, -3) == 6 and
        unsigned_mul(unsigned_max, 1) == unsigned_max and unsigned_mul(unsigned_max, 0) == 0 and small_mul(-16, 1) == -16 and
        negate(-max) == max and signed_cast(-16) == -16 and signed_cast(15) == 15 and
        unsigned_cast(31) == 31 and negative_cast(max) == @as(u64, @intCast(max)) and positive_cast(max) == max and
        divide(unsigned_max, 1) == unsigned_max and remainder(unsigned_max, 1) == 0 and
        shifted == 16 and signed_shifted == -16;
    return @intFromBool(!ok);
}
