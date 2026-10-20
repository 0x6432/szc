const Small=error{A};const Big=error{A,B};
export fn probe()u8{const wide:Big=error.A;const narrow:Small=wide;if(narrow!=error.A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
