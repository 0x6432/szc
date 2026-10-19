// switch-lane: error
// switch-families: 12
const E = enum(u8) { a=1, b=7, c=250 };
fn inner(x:E) u8 { return switch(x) { .a... .b=>1,.c=>3, }; }
export fn probe(x:u8) u8 { return inner(@enumFromInt(x)); }

pub fn main()void{}
