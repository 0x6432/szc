export fn probe(p:*align(1) u8)*?u64{const q:*?u64=@alignCast(@ptrCast(p));return q;}
pub fn main()void{}
