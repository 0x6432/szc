const S = struct {
    text: []const u8,
};
const s: S = .{ .text = "abc" };
pub fn main() void {
    const p = &s.text.len;
    const q = &s.text.ptr;
    const n: u8 = if (p.* == 3 and q.*[1] == 98) 0 else 256;
    _ = n;
}
