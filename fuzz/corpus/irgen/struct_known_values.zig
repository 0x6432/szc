// exit: 0
const Inner = struct { value: u8 };
const S = struct { inner: Inner, p: *const u8, bytes: []const u8 };
const number: u8 = 7;
const object = S{ .bytes = "abc", .inner = .{ .value = 3 }, .p = &number };
fn sum(a: u8, p: *const u8, bytes: []const u8) u8 { return a + p.* + bytes[1]; }
pub fn main() u8 {
    const copy = object;
    const p = &copy;
    return sum(@field(@field(p, "inner"), "value"), p.p, p.bytes) - 108;
}
