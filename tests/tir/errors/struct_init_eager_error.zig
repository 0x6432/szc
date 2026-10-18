const S = struct { a: u8, b: u8 };
pub fn main() void {
    const s = S{ .a = @as(u8, 1) / 0, .unknown = 1 };
    _ = s;
}
