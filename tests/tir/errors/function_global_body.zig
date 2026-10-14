fn bad()u8{return true;}
var p:*const fn()u8=&bad;
pub fn main()void{_=p;}
