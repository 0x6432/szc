export fn probe(address:usize)u8{const f:*const fn()?u8=@ptrFromInt(address);const x=f();_=x;return 1;}
pub fn main()void{}
