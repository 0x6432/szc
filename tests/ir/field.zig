const N=struct {
	const @"space name":u8=7;
	var counter:u8=1;
	fn add(arg:u8) u8 { return arg+1; }
};
pub fn main() u8 {
	@field(N,"counter")+=2;
	const p=&@field(N,"counter");
	if(p!=&N.counter or p.*!=3) return 1;
	return @field(&N,"add")(@field(N,"space name"))-8;
}
