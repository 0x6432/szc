const S = struct {
    a: [2]u8,
};
const s: S = .{ .a = .{ 1, 2 } };
pub fn main() void {
    _ = &s.a[2];
}
