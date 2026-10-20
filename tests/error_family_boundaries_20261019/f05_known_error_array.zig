const E=error{A,B};
export fn probe()u8{const a=[1]E{error.A};return if(a[0]==error.A)1 else 0;}
pub fn main()void{}
