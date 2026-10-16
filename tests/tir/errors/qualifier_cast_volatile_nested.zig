pub fn main() void { const p: *volatile *volatile u8 = undefined; const q: **u8 = @volatileCast(p); _ = q; }
