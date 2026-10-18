const S=struct { a:u8, }; const obj=S{.a=7}; pub fn main() void {const p:*const volatile S=&obj;_=p.*;}
