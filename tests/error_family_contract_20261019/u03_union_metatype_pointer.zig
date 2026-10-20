const U=error{A}!u8;
export fn probe()u8{if(@TypeOf(U)!=type){const bad:u8=false;_=bad;}if(*const U!=*const(error{A}!u8)){const bad:u8=false;_=bad;}if(*U==*const U){const bad:u8=false;_=bad;}if(@TypeOf(@as(*const U,undefined))!=*const U){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
