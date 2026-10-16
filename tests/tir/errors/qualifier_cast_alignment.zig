pub fn main() void { const p: *align(1) const u32 = undefined; const q: *u32 = @constCast(p); _ = q; }
