const E=error{A,B};const F=error{A,C};const G=error{C};
export fn probe()u8{const a:E=error.A;const b:F=error.A;const c:G=error.C;if(a!=b){const bad:u8=false;_=bad;}if(a==c){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
