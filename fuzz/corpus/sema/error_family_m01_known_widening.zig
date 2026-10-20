const Small=error{A};const Big=error{A,B};
export fn probe()u8{const small:Small=error.A;const wide:Big=small;const direct:Big=error.A;if(wide!=direct){const bad:u8=false;_=bad;}if(@TypeOf(wide)!=Big){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
