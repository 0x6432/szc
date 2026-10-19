// exit: 0
// switch-families: 9,10,17,18
const E=enum(u8){a=1,@"b b"=7,@"_"=250};
const Alias=E;
fn f(x:Alias)u8 { return switch(x){
 .a=>|v| blk:{ const typed:E=v; const buf:[@intFromEnum(v)]u8=undefined; _=&buf; break :blk @intFromEnum(typed); },
 .@"b b",.@"_"=>|v| blk:{const typed:E=v;break :blk @intFromEnum(typed);},
}; }
pub fn main()u8 {if(f(.a)!=1 or f(.@"b b")!=7 or f(.@"_")!=250)return 1;return 0;}
