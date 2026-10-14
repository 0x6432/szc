var value: u32 align(64) = 7;
var zero: u64 align(32) = 0;
var pointer: *align(64) u32 align(16) = &value;
var inferred align(8) = @as(u8, 3);
pub fn main() u8 {
    pointer.* += 1; zero = 9; inferred += 1;
    if (value != 8 or zero != 9 or inferred != 4) return 1;
    return 0;
}
