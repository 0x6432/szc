// exit: 0
fn trunc(a: i64, b: i64) i64 {
    return @divTrunc(a, b);
}
fn floor(a: i64, b: i64) i64 {
    return @divFloor(a, b);
}
fn ceil(a: i64, b: i64) i64 {
    return @divCeil(a, b);
}
fn exact(a: i64, b: i64) i64 {
    return @divExact(a, b);
}
fn rem(a: i64, b: i64) i64 {
    return @rem(a, b);
}
fn mod(a: i64, b: i64) i64 {
    return @mod(a, b);
}
fn unsigned_ceil(a: u64, b: u64) u64 {
    return @divCeil(a, b);
}
fn mixed(a: i8, b: u7) i8 {
    return @divTrunc(a, b);
}
pub fn main() u8 {
    const min: i64 = -0x8000_0000_0000_0000;
    const max: u64 = 0xffff_ffff_ffff_ffff;
    const c = @divFloor(-5, -2) + @divCeil(-5, -2) + @mod(5, -2) + @rem(-5, 2);
    const ok = trunc(-5, 2) == -2 and trunc(5, -2) == -2 and floor(-5, 2) == -3 and floor(5, -2) == -3 and
        floor(-5, -2) == 2 and ceil(-5, 2) == -2 and ceil(5, -2) == -2 and ceil(-5, -2) == 3 and
        rem(-5, 2) == -1 and rem(5, -2) == 1 and mod(-5, 2) == 1 and mod(5, -2) == -1 and mod(-5, -2) == -1 and
        rem(min, 1) == 0 and mod(min, 1) == 0 and exact(-6, 2) == -3 and exact(6, -2) == -3 and exact(min, 1) == min and
        unsigned_ceil(max, 2) == (1 << 63) and unsigned_ceil(max, 1) == max and mixed(-5, 2) == -2 and c == 3;
    return @intFromBool(!ok);
}
