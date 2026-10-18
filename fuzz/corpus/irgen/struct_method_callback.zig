fn plus(x:u8)u8{return x+1;}
const S=struct{x:u8=7,cb:*const fn(u8)u8=&plus,fn read(self:*const S)u8{return self.x;}const alias=read;};
pub fn main()u8{var s:S=.{};s.x=5;return s.cb(s.alias());}
