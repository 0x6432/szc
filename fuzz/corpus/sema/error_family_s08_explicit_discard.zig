const E=error{A,B};
export fn direct()u8{if(true){_=error.A;}return 1;}
export fn alias()u8{const x=error.A;_=x;return 1;}
export fn coerced()u8{_= @as(E,error.A);return 1;}
pub fn main()void{}
