// Template expanded by run_struct_mutable_global_storage.py; not a standalone Zig fixture.
const Inner = struct { word: u32, tag: u8 };
const Empty = struct {};
const Root = struct {
    tag: u8,
    wide: u64,
    inner: Inner,
    rows: [2]Inner,
    text: []const u8,
    pointer: *const u8,
    child: *const Inner,
    callback: *const fn (u8) u8,
    sentinel: [2:9]u8,
    empty_sentinel: [0:7]u8,
    flags: bool,
    odd: u9,
    z: [0]u64,
    empty: Empty,
};
const target: u8 = 71;
const child: Inner = .{ .word = 505, .tag = 17 };
fn callback(x: u8) u8 {
    return x + 1;
}
var root: Root align(STORAGE_ALIGNMENT) = .{
    .tag = 11,
    .wide = 0x1122334455667788,
    .inner = .{ .word = 0x12345678, .tag = 12 },
    .rows = .{ .{ .word = 101, .tag = 13 }, .{ .word = 202, .tag = 14 } },
    .text = "abc",
    .pointer = &target,
    .child = &child,
    .callback = &callback,
    .sentinel = .{ 21, 22 },
    .empty_sentinel = .{},
    .flags = true,
    .odd = 257,
    .z = .{},
    .empty = .{},
};

// Separate outer-tail, undefined, zero-anchor and stored typed-path coverage.
const Tail = struct { tag: u8, word: u64 };
var tail: Tail align(STORAGE_ALIGNMENT) = .{ .tag = 23, .word = 0x123456789abcdef0 };
var unknown: Tail align(STORAGE_ALIGNMENT) = undefined;
var empty: Empty align(STORAGE_ALIGNMENT) = .{};
const Bundle = struct { tag: *const u8, wide: *const u64, nested: *const u32, unknown: Tail };
const bundle: Bundle align(STORAGE_ALIGNMENT) = .{
    .tag = &tail.tag, .wide = &tail.word, .nested = &root.rows[1].word, .unknown = undefined,
};
const root_metadata = [_]usize{
    @sizeOf(Root), STORAGE_ALIGNMENT, @sizeOf(Inner), @offsetOf(Inner, "word"), @offsetOf(Inner, "tag"),
    @offsetOf(Root, "tag"), @offsetOf(Root, "wide"), @offsetOf(Root, "inner"), @offsetOf(Root, "rows"),
    @offsetOf(Root, "text"), @offsetOf(Root, "pointer"), @offsetOf(Root, "child"), @offsetOf(Root, "callback"),
    @offsetOf(Root, "sentinel"), @offsetOf(Root, "empty_sentinel"), @offsetOf(Root, "flags"),
    @offsetOf(Root, "odd"), @offsetOf(Root, "z"), @offsetOf(Root, "empty"),
};
const edge_metadata = [_]usize{
    STORAGE_ALIGNMENT, @sizeOf(Tail), @offsetOf(Tail, "tag"), @offsetOf(Tail, "word"),
    @offsetOf(Bundle, "tag"), @offsetOf(Bundle, "wide"), @offsetOf(Bundle, "nested"),
    @sizeOf(Inner), @offsetOf(Inner, "word"), @offsetOf(Bundle, "unknown"),
};
extern fn check_backing(p: *const anyopaque, metadata: [*]const usize) u8;
extern fn check_edges(t: *const anyopaque, b: *const anyopaque, u: *const anyopaque,
    e: *const anyopaque, a: *const anyopaque, metadata: [*]const usize) u8;
extern fn check_storage(r: *anyopaque, t: *anyopaque, u: *anyopaque, e: *anyopaque,
    b: *const anyopaque, rm: [*]const usize, em: [*]const usize) u8;
pub fn main() u8 {
    const result = check_storage(@ptrCast(&root), @ptrCast(&tail), @ptrCast(&unknown),
        @ptrCast(&empty), @ptrCast(&bundle), &root_metadata, &edge_metadata);
    if (result != 0) return result;
    if (root.tag != 33 or root.rows[1].word != 777 or tail.word != 888 or unknown.word != 999) return 121;
    if (root.pointer != &tail.tag) return 122;
    root.tag = 11;
    root.rows[1].word = 202;
    root.pointer = &target;
    tail.word = 0x123456789abcdef0;
    unknown.word = 0;
    const restored = check_backing(@ptrCast(&root), &root_metadata);
    if (restored != 0) return restored;
    return check_edges(@ptrCast(&tail), @ptrCast(&bundle), @ptrCast(&unknown),
        @ptrCast(&empty), @ptrCast(&root.rows), &edge_metadata);
}
