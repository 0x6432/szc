// switch-lane: error
// switch-families: 19
comptime { const x:u8=1; switch(x) { 1=>{},else=>|_|{} } }

pub fn main()void{}
