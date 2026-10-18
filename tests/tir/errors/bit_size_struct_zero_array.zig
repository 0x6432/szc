const S = struct {
    a: anyopaque,
};
pub fn main() void {
    _ = @bitSizeOf([0]S);
}
