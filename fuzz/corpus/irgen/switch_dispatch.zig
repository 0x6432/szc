// exit: 0
// switch-families: 1,2,3
var calls:u8=0;
var bodies:u8=0;
fn operand(x:u8)u8 { calls+=1; return x; }
fn f(x:u8)u8 { return switch(operand(x)) {
  1=>11, 2,4,6,8,=>blk:{bodies+=1; break :blk 22;},
  10...12=>33, 15...15=>44, else=>55,
}; }
pub fn main()u8 {
 if(f(1)!=11 or f(2)!=22 or f(4)!=22 or f(6)!=22 or f(8)!=22) return 1;
 if(f(9)!=55 or f(10)!=33 or f(12)!=33 or f(13)!=55 or f(15)!=44) return 2;
 if(calls!=10 or bodies!=4) return 3;
 return 0;
}
