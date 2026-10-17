const Pair = struct { byte:u8, word:u64, };
const Node = struct { next:*const @This(), pair:Pair, };
pub fn main() u8 {
	const Field = @FieldType(Node, "pair");
	return if (@hasField(Node, "next") and Field == Pair and @sizeOf(Node) == 24 and @alignOf(Node) == 8) 0 else 1;
}
