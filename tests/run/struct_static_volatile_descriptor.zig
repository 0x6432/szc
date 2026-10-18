// exit: 0
const Inner = struct { text: []const u8 };
const Outer = struct { inner: Inner, tag: u64 };
const root: Outer = .{ .inner = .{ .text = "abc" }, .tag = 17 };
noinline fn read(p: *const volatile []const u8) u8 {
    const len = &p.len;
    const ptr = &p.ptr;
    if (len.* != 3) return 1;
    if (ptr.*[1] != 98) return 2;
    return 0;
}
pub fn main() u8 {
    const p: *const volatile Outer = &root;
    const len = &p.inner.text.len;
    const ptr = &@field(@field(@field(p, "inner"), "text"), "ptr");
    if (len.* != 3) return 3;
    if (ptr.*[2] != 99) return 4;
    return read(&root.inner.text);
}
