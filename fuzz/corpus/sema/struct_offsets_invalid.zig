const S=struct { a:[4294967295]u8, b:u8, }; pub fn main() void { _=@offsetOf(S,"missing"); }
