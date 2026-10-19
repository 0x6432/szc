const E=enum{};
export fn probe(p:*const E)usize{return @sizeOf(*const @TypeOf(p.*));}
pub fn main()void{}
