const N=struct{const value:E=E.A;};const Alias=N.value;const E=error{A,B};
export fn probe()u8{if(N.value!=error.A){const bad:u8=false;_=bad;}if(Alias!=E.A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
