const value: u32 align(64) = 42;
const pointer: *align(64) const u32 align(32) = &value;
const inferred align(16) = @as(u16, 7);
const alias = value;
fn read(p: *align(32) const *align(64) const u32) u32 { return p.*.*; }
pub fn main() u8 {
    const P = *align(@as(u29, (&inferred).* + 1)) const u32;
    const p: P = &value;
    if (read(&pointer) != 42 or p.* != 42 or alias != 42) return 1;
    return 0;
}
