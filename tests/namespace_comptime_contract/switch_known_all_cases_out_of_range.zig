// nc-lane: reject
// nc-primary: type 'u8' cannot represent integer value '256'
// switch-lane: error
// switch-families: 14
comptime { const x:u8=1; switch(x) { 1=>{},256=>{},else=>{}, } }
pub fn main()void{}
