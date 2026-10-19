// switch-lane: error
// switch-families: 11
comptime { const x:u8=1; switch(x) { 0...2=>{},2...3=>{},else=>{}, } }
pub fn main()void{}
