const E=enum(u8){a=1,b=2};
fn inner(x:E)u8{return switch(x){.a=>3,.b=>4};}
export fn probe(x:u8)u8{return inner(@enumFromInt(x));}
pub fn main()void{}
