pub fn main() void { const p: *u8 = undefined; const q: *align(16) u8 = @alignCast(p); _ = q; }
