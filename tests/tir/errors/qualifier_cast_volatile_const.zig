pub fn main() void { const p: *const volatile u8 = undefined; const q: *u8 = @volatileCast(p); _ = q; }
