// exit: 93
fn narrow(a: u64) u5 {
    return @truncate(a);
}
fn signed(a: i64) i5 {
    return @truncate(a);
}
fn widen(a: u8) u64 {
    return @truncate(a);
}
fn cast(a: i64) u8 {
    return @intCast(a);
}
fn boolean(a: bool) u8 {
    return @intFromBool(a);
}
fn zero(a: u0) u64 {
    return @intCast(a);
}
pub fn main() u8 {
    const a: u5 = @truncate(65);
    const b: i5 = @truncate(-33);
    const c: u8 = @intCast(7);
    const t: u8 = @intFromBool(true);
    const f: u8 = @intFromBool(false);
    const s: i5 = signed(63);
    const i: i64 = s;
    return cast(@as(i64, @intCast(widen(64))) + i + b) + narrow(95) + a + c - 10 + boolean(true) + boolean(false) + t + f + @as(u8, @intCast(zero(0)));
}
