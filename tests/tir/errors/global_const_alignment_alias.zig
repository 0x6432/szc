const source: u32 align(64) = 7; const value = source;
pub fn main() void { const p: *align(64) const u32 = &value; _ = p; }
