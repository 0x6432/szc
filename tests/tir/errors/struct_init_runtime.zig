const S=struct { a:u8, }; pub fn main() void {var s:S=.{.a=1};const p:*volatile S=&s;p.*=.{.a=2};}
