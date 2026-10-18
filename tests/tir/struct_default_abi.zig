const S=struct{x:u8=7,y:u16=513};
fn make(x:u8)S{return .{.x=x};}
fn copy(s:S)S{return s;}
pub fn main()u8{var s=copy(make(5));s=.{};return s.x;}
