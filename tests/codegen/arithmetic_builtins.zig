fn operation(a: i8, b: i8, shift: u8) i8 {
    const saturated = (a +| b) *| b;
    return @divFloor(saturated <<| shift, b);
}
pub fn main() u8 {
    return @intCast(operation(4, 2, 0));
}
