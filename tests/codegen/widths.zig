// exit: 100
fn wrap_u33(a: u33) u33 {
    return a +% 1;
}
fn wrap_i40(a: i40) i40 {
    return a +% 1;
}
fn shl_u8(a: u8, b: u3) u8 {
    return a << b;
}
fn shr_i16(a: i16, b: u4) i16 {
    return a >> b;
}
fn div_u64(a: u64, b: u64) u64 {
    return a / b;
}
fn rem_u16(a: u16, b: u16) u16 {
    return a % b;
}
fn widen(a: u16) u64 {
    return a;
}
fn widen_signed(a: i8) i64 {
    return a;
}
fn not_u5(a: u5) u5 {
    return ~a;
}
fn xor_i32(a: i32, b: i32) i32 {
    return a ^ b;
}
pub fn main() u8 {
    var total: u64 = 0;
    total = total + wrap_u33(0x1_ffff_ffff); // 0
    const big: i40 = wrap_i40(0x7f_ffff_ffff); // -2^39
    const neg_one: i64 = widen_signed(-1);
    total = total + shl_u8(0xff, 4); // 240
    total = total + widen(rem_u16(1000, 7)); // 6
    total = total + div_u64(1 << 40, 1 << 38); // 4
    total = total + not_u5(1); // 30
    const s: i16 = shr_i16(-64, 3); // -8
    const x: i32 = xor_i32(-1, 5); // -6
    const sum: i64 = s + x + neg_one; // -15
    const all: i64 = sum + big + (1 << 39); // -15
    const result: i64 = all + 15 + 100 - 280;
    // total = 280, result = -180
    total = total - 280;
    return to_u8(total + 100, result + 180);
}
fn to_u8(a: u64, b: i64) u8 {
    const zero: i64 = 0;
    const one: u64 = 1;
    _ = zero;
    _ = one;
    return low(a) + low_signed(b);
}
fn low(a: u64) u8 {
    return @truncate(a);
}
fn low_signed(b: i64) u8 {
    return @intCast(b);
}
