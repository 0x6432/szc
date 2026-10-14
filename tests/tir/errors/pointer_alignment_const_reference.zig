pub fn main() void { const p: *align(16) const u8 = &@as(u8, 1); _ = p; }
