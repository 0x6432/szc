const S = struct {
    item: u8,
    wide: u64,
    pub const declaration: u8 = 256;
    pub fn bad() u8 {
        return 256;
    }
};
pub fn main() void {
    _ = @bitOffsetOf(S, "missing");
}
