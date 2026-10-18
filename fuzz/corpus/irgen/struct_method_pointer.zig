const S=struct{x:u8,fn bump(self:*S,n:u8)u8{self.x+=n;return self.x;}};
const O=struct{items:[2]S};var g:O=.{.items=.{.{.x=1},.{.x=2}}};
noinline fn index()usize{return 1;}
pub fn main()u8{return g.items[index()].bump(3);}
