const Root=@This();
const Namespace=struct {
	const Self=@This();
	const value:u8=7;
	var counter:u8=1;
	fn add(n:u8) u8 { counter+=n; return counter; }
	fn same() bool { return Self==@This() and Self!=Root; }
};
pub fn main() u8 {
	const p=&Namespace.counter;
	p.*=Namespace.value;
	if(!Namespace.same()) return 1;
	return Namespace.add(2)-9;
}
