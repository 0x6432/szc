export fn empty()usize{return @sizeOf(error{});}
export fn singleton()usize{return @sizeOf(error{A});}
export fn probe()usize{return @sizeOf(error{A,B});}
pub fn main()void{}
