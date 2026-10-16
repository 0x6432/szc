pub fn main() void { const x:if(@inComptime())u8 else u16=0; _=x; }
