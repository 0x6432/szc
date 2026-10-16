fn f(p:[]u8) []i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}
