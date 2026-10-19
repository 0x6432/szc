export fn probe(x:u8)u8{return outer:{defer{const y:u8=1;_=y;}const A=[if(true)break :outer x else 1]u8;_=A;break :outer 0;};}
pub fn main()void{}
