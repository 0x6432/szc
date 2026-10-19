// Reached-body counterpart; original global-comptime fixture is retained.
// switch-lane: error
// switch-families: 10
const E=enum(u8){a=1,b=7,c=250};
pub fn main()void { const x:E=.a; switch(x) { .a=>{},.b=>{},.bad=>{},else=>{} } }
