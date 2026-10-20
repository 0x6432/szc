fn unused()void{const x:u8=error.A;_=x;}
export fn probe()u8{if(false){_=error.A;const x:u8=error.B;_=x;}else{_=u8;}return 1;}
pub fn main()void{}
