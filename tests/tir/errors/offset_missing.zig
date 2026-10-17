const S=struct { x:u8, };
pub fn main() void { _=@offsetOf(S,"missing"); }
