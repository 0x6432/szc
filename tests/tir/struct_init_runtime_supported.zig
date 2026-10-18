const S = struct { a: u8 };
fn f(x: u8) void {
    const s = S{ .a = x };
    _ = s;
}
pub fn main() void {
    f(7);
}
