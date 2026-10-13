// exit: 12
fn unsigned(a: u64, b: u64) u8 {
    return @as(u8, @intFromBool(a == b)) + @intFromBool(a != b) + @intFromBool(a < b) + @intFromBool(a > b) + @intFromBool(a <= b) + @intFromBool(a >= b);
}
fn signed(a: i8, b: i8) u8 {
    return @as(u8, @intFromBool(a == b)) + @intFromBool(a != b) + @intFromBool(a < b) + @intFromBool(a > b) + @intFromBool(a <= b) + @intFromBool(a >= b);
}
pub fn main() u8 {
    return unsigned(1 << 63, 1) + unsigned(2, 2) + signed(-1, 1) + signed(-2, -2);
}
