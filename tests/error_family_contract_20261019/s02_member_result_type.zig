const E=error{A,B};
export fn probe()u8{if(@TypeOf(E.A)!=E){const bad:u8=false;_=bad;}if(@TypeOf(E.A)==error{A}){const bad:u8=false;_=bad;}if(E.A!=error.A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
