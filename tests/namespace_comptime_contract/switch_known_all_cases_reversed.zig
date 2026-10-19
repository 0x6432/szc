// nc-lane: reject
// nc-primary: range start value is greater than the end value
// switch-lane: error
// switch-families: 12
comptime { const x:u8=1; switch(x) { 1=>{},5...3=>{},else=>{}, } }
pub fn main()void{}
