const F = fn(u8)u8;
fn first(n:u8)u8{return n+1;}
var pointer:*const F=&first;
fn invoke(p:*const F)u8{return p(7);}
pub fn main()u8{return invoke(pointer);}
