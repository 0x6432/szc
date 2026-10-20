export fn probe()u8{const T=outer:{const ignored=error{A}||break :outer error{B};_=ignored;};if(T!=error{B}){const bad:u8=false;_=bad;}return 1;}
pub fn main()void{}
