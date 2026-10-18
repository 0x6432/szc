const S = struct { a: u8, b: u8 };
pub fn main() void {
    const s = S{ .a = 256, .unknown = 1 };
    _ = s;
}
