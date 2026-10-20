const E=error{A,B};
export fn probe()u8{const a:E=error.A;const b:E=E.A;if(a!=b){const bad:u8=false;_=bad;}if(@TypeOf(a)!=E){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
