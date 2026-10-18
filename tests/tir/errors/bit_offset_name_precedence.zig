const S = struct {
    a: S,
};
pub fn main() void {
    _ = @bitOffsetOf(S, 3);
}
