// Reached-body counterpart; original global-comptime fixture is retained.
// switch-lane: error
// switch-families: 15
pub fn main()void { const x:u8=undefined; switch(x) { 0...255=>{} } }
