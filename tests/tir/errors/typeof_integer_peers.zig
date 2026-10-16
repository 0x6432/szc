fn f(a:u8,b:i8) void { _=@TypeOf(a,b); }
pub fn main() void { f(1,2); }
