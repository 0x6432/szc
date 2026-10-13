const A = B;
const B = A;
fn f(x: u8) u8 {
    const y: u8 = 300;
    var z = 5;
    const w: i32 = x;
    const q: u8 = w;
    _ = x / 0;
    const m: u8 = 255;
    _ = m + 1;
    _ = -x;
    _ = A;
    _ = f(1, 2);
    _ = y;
    _ = z;
    _ = q;
}
fn g() u32 {}
pub fn main() void {
    _ = f(1);
    _ = g();
    const i: i32 = 5;
    _ = i / i;
}
