const A=struct{x:u8=(B{}).x};
const B=struct{x:u8=(A{}).x};
pub fn main()void{const s:A=.{};_=s;}
