// exit: 0
// switch-families: 17,18
fn discrete(x:u8)u8 {return switch(x){3=>|v| blk:{const a:[v]u8=undefined;_=&a;break :blk v;},else=>0};}
fn freeze(p:*u8)u8 {return switch(p.*){
 1,2=>|v| blk:{p.*=90;break :blk v;},
 3...5=>|v| blk:{p.*=91;break :blk v;},
 else=>|v| blk:{p.*=92;break :blk v;},
};}
pub fn main()u8 {
 var x:u8=2; if(freeze(&x)!=2 or x!=90)return 1;
 x=4;if(freeze(&x)!=4 or x!=91)return 2;
 x=7;if(freeze(&x)!=7 or x!=92)return 3;
 if(discrete(3)!=3 or discrete(4)!=0)return 4; return 0;
}
