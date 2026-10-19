// nc-lane: unsupported
// nc-nyi: comptime function calls
fn f(x:u8)u8{return x+1;} comptime {const x=f(2);_=x;}
pub fn main() void {}
