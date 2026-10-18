const I = struct {
    x: u8,
};
const S = struct {
    a: [2]I,
};
const s: S = .{ .a = .{ .{ .x = 1 }, .{ .x = 2 } } };
pub fn main() void {
    const p: [*]const I = @ptrCast(&s.a);
    const n: u8 = if (&p[1].x == &s.a[1].x and &@field(p[1], "x") == &s.a[1].x) 0 else 256;
    _ = n;
}
