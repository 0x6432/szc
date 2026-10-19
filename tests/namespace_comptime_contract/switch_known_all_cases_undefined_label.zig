// nc-lane: reject
// nc-primary: use of undefined value here causes illegal behavior
// switch-lane: error
// switch-families: 14
comptime { const x:u8=1; switch(x) { 1=>{},undefined=>{},else=>{}, } }
pub fn main()void{}
