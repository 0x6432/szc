pub fn main() void { const p: *const *const u8 = undefined; const q: **u8 = @constCast(p); _ = q; }
