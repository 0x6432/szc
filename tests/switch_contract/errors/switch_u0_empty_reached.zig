// Reached-body counterpart; original global-comptime fixture is retained.
// switch-lane: error
// switch-families: 8
pub fn main()void { const x:u0=0; _=switch(x) {}; }
