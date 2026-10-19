// nc-lane: reject
// nc-primary: duplicate switch value '1'
// switch-lane: error
// switch-families: 11
comptime { const x:u8=1; switch(x) { 1=>{},1=>{},else=>{}, } }
pub fn main()void{}
