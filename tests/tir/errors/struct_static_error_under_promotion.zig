const S = struct {
    a: u8,
    wide: u64,
};
const s: S = .{ .a = 7, .wide = 19 };
pub fn main() void {
    const p: *align(1) const S = &s;
    const q: *const u64 = &p.wide;
    _ = q;
}
