const S = struct { first: void, a: u8 };
pub fn main() void {
    const s = S{ .a = 7 };
    _ = s;
}
