// nc-lane: reject
// nc-primary: use of undefined value here causes illegal behavior
// switch-lane: error
// switch-families: 15
comptime { const x:u8=undefined; switch(x) { 0...255=>{} } }
pub fn main()void{}
