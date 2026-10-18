// exit: 0
// Ordinary bit offsets are metadata only: no aggregate storage/ABI involved.
const Padded = struct { big: u64, byte: u8, zero: [0]u8 };
const ZeroAlign = struct { byte: u8, zero: [0]u128 };
const AllZero = struct { a: void, b: u0, c: [0]u64 };
const Inner = struct { byte: u8, word: u32 };
const Outer = struct { inner: Inner, array: [3]Inner, z: void };
const Wide = struct { a: u513, b: i129, c: u24, d: u33, e: [3]u8, f: bool };
const Node = struct { tag: u8, next: *const @This(), children: []@This() };
const Limit = struct { a: [4294967294]u8, b: u8 };

pub fn main() u8 {
    if (@bitOffsetOf(Padded, "big") != 0) return 1;
    if (@bitOffsetOf(Padded, "byte") != 64) return 2;
    if (@bitOffsetOf(Padded, "zero") != 72) return 3;
    if (@sizeOf(Padded) != 16 or @alignOf(Padded) != 8) return 4;
    if (@bitOffsetOf(ZeroAlign, "byte") != 0 or @bitOffsetOf(ZeroAlign, "zero") != 0) return 5;
    if (@sizeOf(ZeroAlign) != 16 or @alignOf(ZeroAlign) != 16) return 6;
    if (@sizeOf(AllZero) != 0 or @alignOf(AllZero) != 8) return 7;
    if (@bitOffsetOf(AllZero, "a") != 0 or @bitOffsetOf(AllZero, "b") != 0 or @bitOffsetOf(AllZero, "c") != 0) return 8;
    if (@bitOffsetOf(Inner, "byte") != 32 or @bitOffsetOf(Inner, "word") != 0) return 9;
    if (@bitOffsetOf(Outer, "array") != 64 or @bitOffsetOf(Outer, "z") != 256) return 10;
    if (@bitOffsetOf(Wide, "b") != 640 or @bitOffsetOf(Wide, "f") != 1016) return 11;
    if (@bitOffsetOf(Node, "tag") != 192 or @bitOffsetOf(Node, "children") != 64) return 12;
    if (@bitOffsetOf(Padded, "byte\x00") != 64) return 13;
    if (@TypeOf(@bitOffsetOf(Padded, "byte")) != comptime_int) return 14;
    if (@bitOffsetOf(Limit, "b") != 34359738352 or @sizeOf(Limit) != 4294967295) return 15;
    if (@bitSizeOf([2]u9) != 18 or @sizeOf([2]u9) != 4) return 16;
    if (@bitSizeOf([0:0]u9) != 9 or @TypeOf(@bitSizeOf(u9)) != comptime_int) return 17;
    return 0;
}
