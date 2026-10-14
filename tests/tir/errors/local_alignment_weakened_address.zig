pub fn main() void { var value: u64 align(1) = 1; value += 0; const p: *u64 = &value; _ = p; }
