const E=enum{};
export fn probe(p:*const E)noreturn{defer{const x:u8=false;_=x;}_=p.*;}
pub fn main()void{}
