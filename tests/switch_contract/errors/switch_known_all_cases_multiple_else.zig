// switch-lane: error
// switch-families: 13
comptime { const x:u8=1; switch(x) { 1=>{},else=>{},else=>{}, } }
pub fn main()void{}
