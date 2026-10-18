const S=struct {a:u8,b:u8,};noinline fn byte(x:u8)u8{return x;}pub fn main()u8{var s:S=.{.a=byte(1),.b=2};const t=s;s.a=7;return t.a-1;}
