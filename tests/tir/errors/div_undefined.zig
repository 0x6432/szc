fn operation(a: i32) i32 {
    return @rem(a, undefined);
}
pub fn main() u8 {
    _ = operation(1);
    return 0;
}
