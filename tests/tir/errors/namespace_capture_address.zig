pub fn main() void {
	var x:u8=7; _=&x;
	const N=struct { fn read() void { _=&x; } };
	N.read();
}
