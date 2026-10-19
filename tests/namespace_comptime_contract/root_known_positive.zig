// nc-lane: accept
comptime {const x:u8=3+4; if(x!=7) @panic("bad constant"); switch(x){7=>{},else=>@panic("bad selection")} }
pub fn main() void {}
