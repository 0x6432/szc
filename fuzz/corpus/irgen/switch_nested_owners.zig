// exit: 0
// switch-families: 19
const E=enum(u8){a=1,b=7};
fn f(x:u8,e:E)u16 {return switch(x){
 1,2=>|outer| blk:{
   const inner:u16=switch(e){.a=>|v| @as(u16,@intFromEnum(v))+outer,.b=>|v| @as(u16,@intFromEnum(v))+outer};
   const known:u8=switch(@as(u8,3)){3=>|v| v,else=>0};
   const again:u8=switch(x){1=>|v| v,else=>|v| v};
   break :blk inner+outer+known+again;
 },
 else=>|outer| blk:{const inner:u16=switch(e){.a=>|v| @intFromEnum(v),.b=>|v| @intFromEnum(v)};break :blk inner+outer;},
};}
pub fn main()u8 {if(f(2,.a)!=10 or f(1,.b)!=13 or f(4,.b)!=11)return 1;return 0;}
