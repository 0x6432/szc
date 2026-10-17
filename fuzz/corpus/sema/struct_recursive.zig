const S=struct { next:*@This(), children:[]S, }; pub fn main() u8 { return if(@hasField(S,"next") and @FieldType(S,"children")==[]S and @sizeOf(S)==24)0 else 1; }
