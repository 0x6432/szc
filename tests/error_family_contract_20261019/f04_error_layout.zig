export fn empty()usize{return @sizeOf(error{});}
export fn single()usize{return @alignOf(error{A});}
export fn many()usize{return @sizeOf(error{A,B});}
pub fn main()void{}
