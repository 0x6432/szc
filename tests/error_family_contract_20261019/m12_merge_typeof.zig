export fn probe()u8{if(@TypeOf(error{A}||error{B})!=type){const bad:u8=false;_=bad;}if(@TypeOf(@as(error{A},error.A))!=error{A}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
