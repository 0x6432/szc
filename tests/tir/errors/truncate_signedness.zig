fn narrow(a: i16) u8 {
    return @truncate(a);
}
pub fn main() u8 {
    return narrow(5);
}
