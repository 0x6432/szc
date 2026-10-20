const M=error{A}||error{B};
export fn probe()u8{const v=M.B;if(v!=error.B){const bad:u8=false;_=bad;}if(@TypeOf(v)!=M){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
