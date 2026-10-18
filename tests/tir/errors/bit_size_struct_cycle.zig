const S = struct {
    a: S,
};
pub fn main() void {
    _ = @bitSizeOf(S);
}
