const E=error{A,B};
export fn probe(p:*const E) bool { const n=@errorName(p.*); return n.len!=0; }
pub fn main() void {}
