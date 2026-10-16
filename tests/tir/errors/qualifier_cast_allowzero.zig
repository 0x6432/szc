pub fn main() void { const p: *allowzero const u8 = undefined; const q: *u8 = @constCast(p); _ = q; }
