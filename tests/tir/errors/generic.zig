fn id(comptime T: type, x: T) T {
    return x;
}
fn bad() void {}
pub fn main() void {
    _ = id(u8, 1);
    bad(1);
    const t = u8;
    var v: t = 1;
    v = v << 8;
    var s: i32 = 1;
    s = s / 2;
    s = s % 2;
    const n: u8 = 1;
    _ = n == true;
    _ = !s;
    _ = ~5;
}
