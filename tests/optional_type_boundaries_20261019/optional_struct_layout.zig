const S=struct{x:?u64};
export fn pointerOnly(p:*const S)*const S{return p;}
export fn layoutDemand()usize{return @sizeOf(S);}
pub fn main()void{}
