// nc-lane: reject
// nc-primary: encountered @panic at comptime: selected switch
comptime {const x:u8=1; switch(x){1=>@panic("selected switch"),else=>{}}}
pub fn main() void {}
