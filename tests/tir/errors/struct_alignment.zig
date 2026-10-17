const S=struct { a:u8,b:u64, };
pub fn main() void { const p:*S=@ptrFromInt(4097); _=p; }
