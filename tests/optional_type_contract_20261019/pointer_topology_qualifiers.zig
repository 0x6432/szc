const A=*?u8;const B=?*u8;
export fn probe()u8{if(A==B){const bad:u8=false;_=bad;}if(?*u8==?*const u8){const bad:u8=false;_=bad;}if(?*const u8==?*const volatile u8){const bad:u8=false;_=bad;}if(*?u8==*const ?u8){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
