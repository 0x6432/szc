const S = struct {
    a: u8,
    wide: u64,
};
const s: S = .{ .a = 7, .wide = 19 };
extern fn consume(p: *const u8, q: *const u64) void;
pub fn main() void {
    consume(&s.a, &s.wide);
}
