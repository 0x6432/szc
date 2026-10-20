const E=error{A,B};const F=error{C,B};
export fn probe()u8{if((E||F)!=error{A,B,C}){const bad:u8=false;_=bad;}if((F||E)!=(E||F)){const bad:u8=false;_=bad;}if((E||error{})!=E){const bad:u8=false;_=bad;}if((error{}||error{})!=error{}){const bad:u8=false;_=bad;}if((error{@"\x41"}||error{A})!=error{A}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
