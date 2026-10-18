const S=struct{x:u8,fn read(self:S,n:u8)u8{return self.x+n;}};pub fn main()void{const s:S=.{.x=7};_=s.read();}
