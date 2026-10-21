export fn single(flag: bool) void { while (flag) @as(u8,1); }
export fn continuation(flag:bool) void { while(flag) : (@as(u8,1)) {} }
export fn positive(flag:bool) void { while(flag) : ({_=@as(u8,1);}) { break; } }
pub fn main() void {}
