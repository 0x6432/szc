const U=error{A}!u8;export fn probe(address:usize)u8{const f:*const fn()U=@ptrFromInt(address);const value=f();_=&value;return 1;}
pub fn main()void{}
