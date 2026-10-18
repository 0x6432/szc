// exit: 0
pub fn main() u8 {
    var empty: [0]u8 = undefined;
    empty = .{};
    if (empty.len != 0) return 1;
    var sentinel: [0:7]u8 = undefined;
    sentinel = .{};
    const p: [*:7]u8 = &sentinel;
    if (p[0] != 7) return 2;
    sentinel = [0:7]u8{};
    if (p[0] != 7) return 3;
    sentinel = @as([0:7]u8, .{});
    if (p[0] != 7) return 4;
    const inferred = [_:7]u8{};
    if (inferred.len != 0) return 5;
    const q: [:7]const u8 = &.{};
    if (q.len != 0 or q.ptr[0] != 7) return 6;
    return 0;
}
