// Reached-body counterpart; original global-comptime fixture is retained.
// switch-lane: error
// switch-families: 11
pub fn main()void { const x:u8=1; switch(x) { 0...2=>{},2...3=>{},else=>{}, } }
