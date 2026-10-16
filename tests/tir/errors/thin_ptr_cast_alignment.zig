fn f(p:*u8) *u16 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}
