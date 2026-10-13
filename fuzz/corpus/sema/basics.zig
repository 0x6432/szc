const big = 0x1_0000_0000_0000_0000;
pub fn add(a: u32, b: u32) u32 {
    var c: u32 = a + b * 2;
    c = c - 1;
    return c;
}
fn noisy(comptime T: type, x: T) void {
    _ = x;
    const y = -%x;
    _ = add(1, 2) == 3 and !true;
}
test "t" {}
