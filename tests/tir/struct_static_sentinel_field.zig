const S = struct {
    a: [2:9]u8,
};
const s: S = .{ .a = .{ 3, 5 } };
pub fn main() void {
    const p = &s.a[2];
    const n: u8 = if (p.* == 9 and @TypeOf(&s.a) == *const [2:9]u8) 0 else 256;
    _ = n;
}
