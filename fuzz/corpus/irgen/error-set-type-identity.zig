const E=error{A,B};const F=error{B,A};const Alias=E;
export fn probe()u8{if(E!=F){const bad:u8=false;_=bad;}if(E!=Alias){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
