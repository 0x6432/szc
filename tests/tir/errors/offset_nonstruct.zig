const S=struct { x:u8, };
pub fn main() void { _=@offsetOf([0]S,"x"); }
