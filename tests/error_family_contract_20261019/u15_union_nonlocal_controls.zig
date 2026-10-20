export fn broken()u8{const T=outer:{const ignored=error{A}!(break :outer u16);_=ignored;};if(T!=u16){const bad:u8=false;_=bad;}return 1;}
export fn continuing(n:u8)u8{var i:u8=0;while(i<n):(i+=1){const T=error{A}!(continue);_=T;}return i;}
pub fn main()void{}
