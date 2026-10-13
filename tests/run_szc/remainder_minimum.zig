// exit: 0
// The official x86_64 backend traps here; its comptime evaluator returns zero.
fn rem(a: i64, b: i64) i64 {
    return @rem(a, b);
}
fn mod(a: i64, b: i64) i64 {
    return @mod(a, b);
}
pub fn main() u8 {
    const min: i64 = -0x8000_0000_0000_0000;
    return @intFromBool(!(rem(min, -1) == 0 and mod(min, -1) == 0));
}
