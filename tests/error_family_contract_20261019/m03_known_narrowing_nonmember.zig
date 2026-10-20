const Small=error{A};const Big=error{A,B};
export fn probe()u8{const wide:Big=error.B;const narrow:Small=wide;return if(narrow==error.A)1 else 0;}
pub fn main()void{}
