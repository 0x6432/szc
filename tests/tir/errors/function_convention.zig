extern fn abs(n:i32)i32;
pub fn main()void{const p:*const fn(i32)i32=&abs;_=p;}
