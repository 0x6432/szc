const E=error{@"a b",@"error"};const F=error{@"error",@"a b"};
export fn probe()u8{if(E!=F){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
