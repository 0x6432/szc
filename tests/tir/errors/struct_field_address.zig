const S=struct {a:u8,};const T=struct {a:u8,};const s=S{.a=1};pub fn main()void{const p:*const T=@ptrCast(&s);_= &p.a;}
