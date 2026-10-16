fn f(p:[]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}
