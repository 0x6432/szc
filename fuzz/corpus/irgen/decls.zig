//! Container doc comment.
const std = @import("std");
pub const A = struct {
    /// A field.
    x: u32 = 0,
    comptime y: u8 = 1,
    z: *align(4) const u8,

    pub fn get(self: A) u32 {
        return self.x;
    }
};
const E = enum(u8) { a, b = 2, _ };
const U = union(enum) { int: i64, none };
const P = packed struct(u16) { lo: u8, hi: u8 };
const T = struct { u32, bool };
extern "c" fn write(fd: c_int, buf: [*]const u8, len: usize) isize;
export fn exported() callconv(.c) void {}
threadlocal var counter: usize = 0;
pub extern var errno: c_int;
inline fn twice(comptime T2: type, x: T2) T2 {
    return x * 2;
}
fn generic(a: anytype, ...) noreturn {}
test "a test" {}
test identifier_test {}
comptime {
    _ = A;
}
