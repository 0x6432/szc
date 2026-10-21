fn v() void {}
export fn probe(flag:bool,x:u8) void { if (flag) v() else v(); switch(x) { 0=>v(), else=>v(), } }
export fn consume(flag:bool,x:u8) u8 { const a:u8=if(flag) @as(u8,1) else @as(u8,2); const b:u8=switch(x){0=>@as(u8,3),else=>@as(u8,4),}; return a+b; }
export fn discard(flag:bool,x:u8) void { _=if(flag) @as(u8,1) else @as(u8,2); _=switch(x){0=>@as(u8,3),else=>@as(u8,4),}; }
pub fn main() void {}
