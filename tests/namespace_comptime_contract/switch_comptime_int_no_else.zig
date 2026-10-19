// nc-lane: reject
// nc-primary: else prong required when switching on type 'comptime_int'
// switch-lane: error
// switch-families: 7
comptime { switch(1) { 1=>{} } }
pub fn main()void{}
