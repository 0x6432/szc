// exit: 0
// switch-families: 2,24
var calls:u16=0;
fn body(x:u8)u16 {calls+=1;return @as(u16,x)*3;}
fn f(x:u8)u16 {return switch(x){0,2,4,6,8,10,12,14,=>body(x),1,3,5,7,9,11,13,15,=>body(x)+1,else=>body(x)+2};}
pub fn main()u8 {var i:u8=0;while(i<20):(i+=1){const want:u16=@as(u16,i)*3+(if(i>=16)@as(u16,2) else if(i%2==1)@as(u16,1) else @as(u16,0));if(f(i)!=want)return 1;}if(calls!=20)return 2;return 0;}
