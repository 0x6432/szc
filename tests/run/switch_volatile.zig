// exit: 0
// switch-families: 1,18
fn f(p:*volatile u8)u8 {return switch(p.*){1,2=>|v| blk:{p.*=99;break :blk v;},3...5=>|v| blk:{p.*=98;break :blk v;},else=>|v| blk:{p.*=97;break :blk v;}};}
pub fn main()u8 {var x:u8=2;if(f(&x)!=2 or x!=99)return 1;x=4;if(f(&x)!=4 or x!=98)return 2;x=7;if(f(&x)!=7 or x!=97)return 3;return 0;}
