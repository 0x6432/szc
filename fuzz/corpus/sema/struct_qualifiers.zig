const S=struct { p:@TypeOf(@as(*const @This(),@as(*@This(),undefined))), a:u8, }; pub fn main() u8 { return @sizeOf(S)-16; }
