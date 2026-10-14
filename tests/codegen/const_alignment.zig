fn read(p: *align(64) const u32) u32 { return p.*; }
fn runtime(n: u32, c: bool) u32 {
    const value: u32 align(64) = n;
    const p = if (c) &value else &value;
    return p.*;
}
pub fn main() u8 {
    const value: u32 align(64) = 42;
    const p: *align(64) const u32 align(32) = &value;
    const size align(16) = 32;
    const P = *align((&size).*) const u32;
    const q: P = &value;
    if (read(p) != 42 or q.* != 42 or runtime(7, false) != 7) return 1;
    return 0;
}
