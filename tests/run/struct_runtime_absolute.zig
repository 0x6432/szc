// exit: 0
const I = struct { x: u16 };
const S = struct { tag: u64, items: [2]I };
const Holder = struct { field: *u16 };
const p: *S = @ptrFromInt(4096);
const holder: Holder = .{ .field = &p.items[1].x };
noinline fn address(q: *u16) usize {
    return @intFromPtr(q);
}
noinline fn stored(q: *const Holder) usize {
    return @intFromPtr(q.field);
}
pub fn main() u8 {
    const expected = 4096 + @offsetOf(S, "items") + @sizeOf(I) + @offsetOf(I, "x");
    if (address(&p.items[1].x) != expected) return 1;
    if (stored(&holder) != expected) return 2;
    return 0;
}
