fn f(b:bool) u8 { _=@TypeOf(if(b) return 99 else @as(u8,3)); return 4; } pub fn main() void { _=f(true); }
