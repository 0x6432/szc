pub fn main() void {
	const x:u8=7;
	const N=struct { fn read() u8 { return x; } };
	_=N.read();
}
