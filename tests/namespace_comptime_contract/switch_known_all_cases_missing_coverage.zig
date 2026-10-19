// nc-lane: reject
// nc-primary: switch must handle all possibilities
// switch-lane: error
// switch-families: 13
comptime { const x:u8=1; switch(x) { 1=>{}, } }
pub fn main()void{}
