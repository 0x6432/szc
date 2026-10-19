const E=error{A,B};const F=error{A};
export fn probe()u8{if(E==F){const bad:u8=false;_=bad;}if(F==error{B}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
