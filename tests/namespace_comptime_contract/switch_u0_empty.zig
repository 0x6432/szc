// nc-lane: reject
// nc-primary: switch must handle all possibilities
// switch-lane: error
// switch-families: 8
comptime { const x:u0=0; _=switch(x) {}; }
pub fn main()void{}
