export fn probe()u8{const n:u8=1;_=n;_=u8;_=error{A};const a=[0]u8{};const p=&a;const q=p++p;_=q;return 1;}
export fn terminating()u8{outer:{_=break :outer;}return 1;}
export fn continuing(n:u8)u8{var i:u8=0;while(i<n):(i+=1){_=continue;}return i;}
pub fn main()void{}
