// exit: 77
fn wrap_add(a: u8, b: u8) u8 {
    return a +% b;
}
fn wrap_sub(a: u8, b: u8) u8 {
    return a -% b;
}
fn wrap_mul_i8(a: i8, b: i8) i8 {
    return a *% b;
}
fn neg(a: i8) i8 {
    return -%a;
}
fn bits(a: u3) u3 {
    return a +% 1;
}
fn is_zero_i8(a: i8) bool {
    return a == 0;
}
pub fn main() u8 {
    var ok: u8 = 0;
    ok = ok + wrap_add(200, 100);
    ok = ok + (wrap_sub(1, 2) - 254);
    const m: i8 = wrap_mul_i8(64, 4);
    const n: i8 = neg(-128);
    const b = bits(7);
    ok = ok + b + 32;
    if_true(is_zero_i8(m) and n < 0);
    return ok;
}
fn if_true(c: bool) void {
    _ = c;
}
