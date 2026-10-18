// exit: 0
// Comptime typed provenance is distinct from physical integer address equality.
const S = struct { a: void, b: u0, c: [0]u8, x: u8 };
const s: S = .{ .a = {}, .b = 0, .c = .{}, .x = 7 };
noinline fn address(p: *const anyopaque) usize {
    return @intFromPtr(p);
}
noinline fn sameAddress(a: *const anyopaque, b: *const anyopaque) bool {
    return a == b;
}
pub fn main() u8 {
    const a: *const anyopaque = @ptrCast(&s.a);
    const b: *const anyopaque = @ptrCast(&s.b);
    const c: *const anyopaque = @ptrCast(&s.c);
    const root: *const anyopaque = @ptrCast(&s);
    const x: *const anyopaque = @ptrCast(&s.x);
    if (a == b or b == c or root == x) return 1;
    if (address(a) != address(b) or address(b) != address(c)) return 2;
    if (address(root) != address(x)) return 3;
    if (!sameAddress(a, b) or !sameAddress(root, x)) return 4;
    return 0;
}
