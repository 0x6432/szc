const S = struct {
    a: u8,
    wide: u64,
};
pub fn main() void {
    const p: *const S = undefined;
    _ = &p.a;
}
