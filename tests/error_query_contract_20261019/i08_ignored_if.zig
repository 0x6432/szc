export fn probe(flag: bool) void { if (flag) @as(u8,1) else @as(u8,2); }
export fn missing_else(flag:bool) void { if(flag) @as(u8,4); }
pub fn main() void {}
