const S=struct{x:u8,fn read(self:S)u8{return self.x;}};pub fn main()void{const s:S=.{.x=7};const f=s.read;_=f();}
