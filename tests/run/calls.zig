// exit: 33
fn add(a: u32, b: u32) u32 {
    return a + b;
}
fn twice(x: u32) u32 {
    return add(x, x);
}
fn small(x: u32) u8 {
    const y: u8 = 3;
    _ = x;
    return y;
}
pub fn main() u8 {
    const r = twice(15);
    return small(r) + 30;
}
