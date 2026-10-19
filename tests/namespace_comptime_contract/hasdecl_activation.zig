// nc-lane: reject
// nc-primary: encountered @panic at comptime: hasdecl activates
const N=struct{pub const x=1;comptime{@panic("hasdecl activates");}}; comptime{_= @hasDecl(N,"x");}
pub fn main() void {}
