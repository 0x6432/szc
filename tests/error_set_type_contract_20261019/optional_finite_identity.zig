const One=?error{A};const Empty=?error{};
export fn probe()u8{if(One!=?error{A}){const bad:u8=false;_=bad;}if(Empty!=?error{}){const bad:u8=false;_=bad;}if(One==Empty){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
