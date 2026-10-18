// exit: 0
const S = struct { a: [3:9]u8, before: u64 };
const s: S = .{ .a = .{ 11, 22, 33 }, .before = 123 };
const Empty = struct { a: [0:7]u8, before: u64 };
const empty: Empty = .{ .a = .{}, .before = 123 };
pub fn main() u8 {
    const p: [*]const u8 = @ptrCast(&s.a);
    if (p[3] != 9) return 1;
    const q = p + 3;
    if ((q - 3)[0] != 11 or q - p != 3) return 2;
    const end = p + 4; // formation/subtraction, NOT a dereference
    if (end - p != 4) return 3;
    const view = p[0..3 :9];
    if (view[3] != 9 or view[1] != 22) return 4;
    const z: [*]const u8 = @ptrCast(&empty.a);
    if (z[0] != 7) return 5;
    return 0;
}
