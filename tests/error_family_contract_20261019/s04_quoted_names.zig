const E=error{@"if",@"two words",A};
export fn probe()u8{if(error.@"if"!=E.@"if"){const bad:u8=false;_=bad;}if(error.@"two words"!=E.@"two words"){const bad:u8=false;_=bad;}if(error.@"\x41"!=E.A){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
