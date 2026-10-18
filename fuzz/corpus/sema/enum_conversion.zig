const E=enum(u8){small=1,mid=7,large=250};
noinline fn decode(n:u16)E{return @enumFromInt(n);}
pub fn main()u8{return @intFromEnum(decode(7));}
