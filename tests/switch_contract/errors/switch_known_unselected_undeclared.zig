// switch-lane: error
// switch-families: 16
comptime { const x:u8=1; switch(x) { 1=>{}, else=>{ _=not_declared; } } }
pub fn main()void{}
