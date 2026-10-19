const E=error{A};
export fn probe()u8{if(*E==*const E){const bad:u8=false;_=bad;}if(*const E==*const volatile E){const bad:u8=false;_=bad;}if(*const E!=*const error{A}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
