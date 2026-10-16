pub fn main() void { _=@hasField([]u8,if(@inComptime())"ptr" else "len"); }
