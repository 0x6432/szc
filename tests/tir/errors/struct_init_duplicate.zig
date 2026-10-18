const S = struct { a: u8 };
pub fn main() void {
    const s = S{ .a = missing, .a = other };
    _ = s;
}
