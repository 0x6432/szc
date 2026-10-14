// exit: 0
// szc links libc through cc; the reference needs explicit -lc.
extern "c" fn abs(n: i32) i32;
var pointer = &abs;
pub fn main() u8 { return if (pointer(-41) == 41) 0 else 1; }
