const Link = struct { next: *Link, value: u8 };
var first: Link = .{ .next = &second, .value = 1 };
var second: Link = .{ .next = &first, .value = 2 };
pub fn main() u8 {
	first.next.value = 7;
	return second.next.value + second.value;
}
