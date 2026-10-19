const A=?comptime_float;
export fn probe()u8{if(A!=?comptime_float){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
