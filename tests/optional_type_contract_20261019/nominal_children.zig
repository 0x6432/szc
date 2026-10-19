const A=struct{};const B=struct{};
export fn probe()u8{if(?A==?B){const bad:u8=false;_=bad;}if(?A!=?A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
