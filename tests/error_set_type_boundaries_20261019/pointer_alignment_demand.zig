const E=error{A,B};export fn probe(p:*align(1) u8)*E{const q:*E=@alignCast(@ptrCast(p));return q;}
export fn empty(p:*align(1)u8)*error{}{const q:*error{}=@alignCast(@ptrCast(p));return q;}
pub fn main()void{}
