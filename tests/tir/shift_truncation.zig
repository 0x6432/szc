const a: u5 = @as(u5, 31) << 4;
const b: i5 = @as(i5, -1) << 4;
pub fn main() u8 {
    return a + @as(u8, @intCast(@as(i8, b) + 16));
}
