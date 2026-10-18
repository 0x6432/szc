const S = struct {
    a: u8 = 256,
};
pub fn main() void {
    _ = @bitSizeOf(S);
}
