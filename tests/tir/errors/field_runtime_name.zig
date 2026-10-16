const N=struct { const x:u8=7; }; fn f(name:[]const u8) u8 { return @field(N,name); } pub fn main() void { _=f("x"); }
