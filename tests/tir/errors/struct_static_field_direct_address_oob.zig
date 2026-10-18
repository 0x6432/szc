const S = struct { a: [3:9]u8, before: u64 };
const s: S = .{ .a = .{ 11, 22, 33 }, .before = 123 };
pub fn main() void { const p: [*]const u8 = @ptrCast(&s.a); const q = &s.a[4]; const n: u8 = if (q == p + 4) 0 else 256; _ = n; }
