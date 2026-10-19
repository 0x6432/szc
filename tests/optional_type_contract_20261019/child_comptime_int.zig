const A=?comptime_int;
export fn probe()u8{if(A!=?comptime_int){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
