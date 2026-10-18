const S=struct{x:u8,fn value(self:S,n:u8)u8{_=n;return self.x;}fn read(self:*const S,n:u8)u8{_=n;return self.x;}};
var g:S=.{.x=1};noinline fn root()*S{return &g;}noinline fn mutate()u8{g.x=9;return 0;}
pub fn main()u8{const a=root().value(mutate());g.x=1;return a+root().read(mutate());}
