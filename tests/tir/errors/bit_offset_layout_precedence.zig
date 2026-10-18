const S = struct {
    a: anyopaque,
};
pub fn main() void {
    _ = @bitOffsetOf(S, "missing");
}
