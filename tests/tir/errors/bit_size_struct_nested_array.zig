const S = struct {
    a: u8,
};
pub fn main() void {
    _ = @bitSizeOf([0][2]S);
}
