const E=enum{};
export fn probe(p:*const E,b:bool)u8{return if(b)blk:{_=p.*;break :blk 1;}else 2;}
pub fn main()void{}
