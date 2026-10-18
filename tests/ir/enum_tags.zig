const E=enum(u9){first=1,second=257,last=511,pub const answer:u8=7;};
pub fn main()u8{const e:E=.second;const n:u8=if(e==E.second and @hasField(E,"first") and !@hasDecl(E,"first") and @bitSizeOf(E)==9)E.answer else 256;return n;}
