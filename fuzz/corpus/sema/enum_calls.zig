const E=enum(u1){a,b};fn flip(e:E)E{return if(e==.a).b else .a;}
fn call(p:*const fn(E)E,e:E)E{return p(e);}
pub fn main()u8{return @intFromEnum(call(&flip,.a));}
