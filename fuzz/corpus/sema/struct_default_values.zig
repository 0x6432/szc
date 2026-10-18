const I=struct{x:u8=7,y:u16=513};
const S=struct{i:I=.{},a:[2]u8=.{3,5},p:*const u8=&byte};
const byte:u8=11;
const s:S=.{};
pub fn main()u8{return s.i.x+s.a[1]+s.p.*;}
