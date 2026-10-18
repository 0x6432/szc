// exit: 0
const Tiny=enum(u1){zero,one,};
const Byte=enum(u8){one=1,seven=7,last=255,};
const Nine=enum(u9){one=1,high=257,last=511,};
const Wide=enum(u33){one=1,high=4294967297,last=8589934591,};
const Full=enum(u64){one=1,high=9223372036854775809,last=18446744073709551615,};
noinline fn tiny(n:u8)Tiny{return @enumFromInt(n);}
noinline fn byte(n:u16)Byte{return @enumFromInt(n);}
noinline fn nine(n:u16)Nine{return @enumFromInt(n);}
noinline fn wide(n:u64)Wide{return @enumFromInt(n);}
noinline fn full(n:u64)Full{return @enumFromInt(n);}
noinline fn choice(b:bool)Byte{return if(b).seven else .last;}
noinline fn round(e:Full)u64{return @intFromEnum(e);}
pub fn main()u8{
 if(tiny(0)!=.zero or tiny(1)!=.one)return 1;
 if(byte(1)!=.one or byte(7)!=.seven or byte(255)!=.last)return 2;
 if(nine(257)!=.high or @intFromEnum(nine(511))!=511)return 3;
 if(wide(4294967297)!=.high or @intFromEnum(wide(8589934591))!=8589934591)return 4;
 if(full(9223372036854775809)!=.high)return 5;
 if(round(full(18446744073709551615))!=18446744073709551615)return 6;
 if(@intFromEnum(choice(true))!=7 or choice(false)!=.last)return 7;
 var e=byte(1);e=byte(7);if(e!=.seven)return 8;
 const Alias=Nine;const alias:Alias=nine(257);if(alias!=Nine.high)return 9;
 if(@sizeOf(Wide)!=8 or @alignOf(Nine)!=2 or @bitSizeOf(Full)!=64)return 10;
 return 0;
}
