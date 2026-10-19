const E=enum{};
export fn probe(p:*const E)noreturn{_=(blk:{const x:u8=false;_=x;break :blk p;}).*;}
pub fn main()void{}
