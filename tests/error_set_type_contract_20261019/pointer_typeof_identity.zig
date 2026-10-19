const E=error{A};export fn probe(p:*const E)u8{if(@TypeOf(p)!=*const error{A}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
