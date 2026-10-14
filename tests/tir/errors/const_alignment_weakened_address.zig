pub fn main() void { const value: u64 align(1) = 1; _ = &value; const p: *const u64 = &value; _ = p; }
