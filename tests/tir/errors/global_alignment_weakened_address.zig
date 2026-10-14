var value: u64 align(1) = 1;
pub fn main() void { const p: *u64 = &value; _ = p; }
