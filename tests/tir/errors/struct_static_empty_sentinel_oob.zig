const S = struct { a: [0:7]u8, before: u64 };
const s: S = .{ .a = .{}, .before = 123 };
pub fn main() void { const p: [*]const u8 = @ptrCast(&s.a); const n = p[1]; _ = n; }
