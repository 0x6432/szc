const S = struct { a: u8, z: void, empty: [0]u64, bits: u0, b: u64 };
const Huge = struct { a: [4294967294]u8, b: u8 };
pub fn main() u8 {
    const bit_offset = @bitOffsetOf(S, "z");
    const large_offset = @bitOffsetOf(Huge, "b");
    if (@TypeOf(bit_offset) != comptime_int or bit_offset != 72) return 1;
    if (large_offset != 34359738352) return 2;
    return @intCast(@bitOffsetOf(S, "a") / 8);
}
