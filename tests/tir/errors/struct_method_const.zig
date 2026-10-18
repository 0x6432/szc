const S=struct{x:u8,fn bump(self:*S)void{self.x+=1;}};pub fn main()void{const s:S=.{.x=7};s.bump();}
