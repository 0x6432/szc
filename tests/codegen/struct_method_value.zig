const S=struct{x:u8=7,y:u16=513,fn copy(self:S,n:u8)S{return .{.x=self.x+n,.y=self.y};}};
noinline fn seed(n:u8)u8{return n;}
pub fn main()u8{const s:S=.{.x=seed(1)};return s.copy(2).copy(3).x;}
