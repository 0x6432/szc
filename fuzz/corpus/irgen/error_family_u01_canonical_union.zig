const E=error{A,B};const U=E!u8;const Alias=U;const Broad=anyerror!u8;
export fn probe()u8{if(U!=(error{B,A}!u8)){const bad:u8=false;_=bad;}if(Alias!=U){const bad:u8=false;_=bad;}if(Broad!=(anyerror!u8)){const bad:u8=false;_=bad;}if(Broad==U){const bad:u8=false;_=bad;}if(@TypeOf(Broad)!=type){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
