const S = struct {
    a: u8,
    b: u64,
};
pub fn main() void {
    _ = @bitSizeOf(S);
}
