const N=struct{const E=Alias;const Alias=error{B,A};};
export fn probe()u8{if(N.E!=error{A,B}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
