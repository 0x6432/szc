const alignment: u29 = 32 * 2;
fn add(p: *align(64) u32) u32 { defer p.* += 1; p.* += 2; return p.*; }
pub fn main() u8 {
    var value: u32 align(alignment) = 5;
    value += 1;
    var weak: u64 align(1) = 9;
    weak += 1;
    var natural: u32 align(4) = 2;
    natural += 1;
    var inferred align(128) = @as(u16, 7);
    inferred += 1;
    const p: *align(64) u32 = &value;
    if (add(p) != 8 or value != 9) return 1;
    if (weak != 10 or natural != 3 or inferred != 8) return 2;
    return 0;
}
