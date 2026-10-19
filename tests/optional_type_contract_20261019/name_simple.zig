const T=?u8;
export fn probe()u8{if(@typeName(T).len!=3){const bad:u8=false;_=bad;}if(@typeName(T)[0]!=63){const bad:u8=false;_=bad;}if(@typeName(T)[1]!=117){const bad:u8=false;_=bad;}if(@typeName(T)[2]!=56){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
