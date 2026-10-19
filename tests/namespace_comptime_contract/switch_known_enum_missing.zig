// nc-lane: reject
// nc-primary: switch must handle all possibilities
// switch-lane: error
// switch-families: 13
const E=enum(u8){a=1,b=7,c=250};
comptime { const x:E=.a; switch(x) { .a=>{} } }

pub fn main()void{}
