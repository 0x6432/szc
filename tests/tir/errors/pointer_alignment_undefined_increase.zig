pub fn main() void { const p: *align(1) u32 = undefined; const q: *u32 = p; _ = q; }
