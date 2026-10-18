// Debug safety fixture: valid exits0; all named invalid modes must panic.
// Checker substitutes this scalar selector; no optional/argv/libc domain.
const selected_mode:u8=0;
const E=enum(u8){one=1,seven=7,last=250,};
const W=enum(u64){one=1,high=9223372036854775815,last=18446744073709551615,};
noinline fn decode(n:u16)E{return @enumFromInt(n);}
noinline fn signed(n:i16)E{return @enumFromInt(n);}
noinline fn wide(n:u64)W{return @enumFromInt(n);}
noinline fn operation(mode:u8)u8{
  if(mode==1)return @intFromEnum(decode(2));
  if(mode==2)return @intFromEnum(decode(257));
  if(mode==3)return @intFromEnum(signed(-1));
  if(mode==4)return if(wide(9223372036854775809)==.one)1 else 2;
  if(mode==5)return if(wide(18446744073709551614)==.one)1 else 2;
 if(decode(1)!=.one or decode(7)!=.seven or decode(250)!=.last)return 1;
 if(wide(9223372036854775815)!=.high or wide(18446744073709551615)!=.last)return 2;
 return 0;
}
pub fn main()u8{return operation(selected_mode);}
