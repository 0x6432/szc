export fn probe()u8{if(@TypeOf(error.A)!=error{A}){const bad:u8=false;_=bad;}if(error.A!=error.A){const bad:u8=false;_=bad;}if(error.A==error.B){const bad:u8=false;_=bad;}if(error.@"\x41"!=error.A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
