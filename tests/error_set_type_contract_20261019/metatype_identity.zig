const E=error{A,B};
export fn probe()u8{if(@TypeOf(E)!=type){const bad:u8=false;_=bad;}if(@TypeOf(error{B,A})!=type){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
