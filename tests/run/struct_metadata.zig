// exit: 0
// Type/layout queries only: deliberately no ordinary-struct runtime values.
const Empty = struct {};
const Zero = struct { a:void, b:u0, };
const Inner = struct { byte:u8, word:u32, };
const Outer = struct { cells:[3]Inner, matrix:[2][3]u16, marker:void, };
const Ordered = struct { a:u8, b:u64, c:u16, };
const Reversed = struct { c:u16, b:u64, a:u8, };
const Odd = struct { a:u9, b:i17, c:u33, };
const Wide = struct { a:u65, b:u8, };
const Node = struct { next:*const @This(), many:[*]const Node, };
const Names = struct {
	@"two words":u16,
	@"fn":u8,
	@"a\"b":u32,
	@"\x61":u8,
	pub const declaration:u8=256;
	pub fn bad() u8 { return 256; }
};
const EmptyArray = struct { a:[0]u64, b:u8, };
const VoidArray = struct { a:[4]void, b:u8, };
const Pointer = struct { a:*align(32) u8, b:u8, };
const Field = @FieldType(Outer,"cells");
pub fn main() u8 {
	if(@sizeOf(Empty)!=0 or @alignOf(Empty)!=1) return 1;
	if(@sizeOf(Zero)!=0 or @alignOf(Zero)!=1) return 2;
	if(!@hasField(Zero,"a") or !@hasField(Zero,"b") or @FieldType(Zero,"a")!=void) return 3;
	if(@sizeOf(Inner)!=8 or @alignOf(Inner)!=4) return 4;
	if(@sizeOf(Outer)!=36 or @alignOf(Outer)!=4) return 5;
	if(Field!=[3]Inner or @FieldType(Outer,"matrix")!=[2][3]u16) return 6;
	if(@sizeOf(Ordered)!=16 or @sizeOf(Reversed)!=16 or @alignOf(Ordered)!=8) return 7;
	if(@sizeOf(Odd)!=16 or @alignOf(Odd)!=8) return 8;
	if(@sizeOf(Wide)!=32 or @alignOf(Wide)!=16) return 9;
	if(@FieldType(Node,"next")!=*const Node or @FieldType(Node,"many")!=[*]const Node) return 10;
	if(@sizeOf(Node)!=16 or @alignOf(Node)!=8) return 11;
	if(!@hasField(Names,"two words") or @FieldType(Names,"two words")!=u16) return 12;
	if(!@hasField(Names,"fn") or @FieldType(Names,"a\"b")!=u32 or !@hasField(Names,"a")) return 13;
	if(@hasField(Names,"declaration") or @hasField(Names,"bad")) return 14;
	if(!@hasField(Names,"fn\x00") or @FieldType(Names,"fn\x00")!=u8) return 15;
	if(@hasField(Names,"") or @hasField(Names,"\x00fn") or @hasField(Names,"Fn") or @hasField(Names,"fn\x00tail")) return 16;
	if(@sizeOf(EmptyArray)!=8 or @alignOf(EmptyArray)!=8) return 19;
	if(@sizeOf(VoidArray)!=1 or @alignOf(VoidArray)!=1) return 20;
	if(@sizeOf(Pointer)!=16 or @alignOf(Pointer)!=8) return 21;
	if(@TypeOf(@hasField(Outer,"cells"))!=bool or @TypeOf(@FieldType(Outer,"cells"))!=type) return 22;
	return 0;
}
