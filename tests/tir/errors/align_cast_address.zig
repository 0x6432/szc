pub fn main() void { const p: *u8 = @ptrFromInt(17); const q: *align(16) u8 = @alignCast(p); _ = q; }
