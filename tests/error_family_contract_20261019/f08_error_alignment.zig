const E=error{A,B};export fn probe(p:*align(1)u8)*E{return @ptrCast(@alignCast(p));}
pub fn main()void{}
