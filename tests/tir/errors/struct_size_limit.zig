const S=struct { a:[4294967287]u8, b:u64, };
pub fn main() void { _=@offsetOf(S,"missing"); }
