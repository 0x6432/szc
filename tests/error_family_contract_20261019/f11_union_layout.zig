const U=error{A}!u64;
export fn size()usize{return @sizeOf(U);}
export fn alignment()usize{return @alignOf(U);}
export fn zero()usize{return @sizeOf([0](error{A}!u8));}
pub fn main()void{}
