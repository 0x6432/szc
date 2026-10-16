fn f(p:[*]u8) []u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}
