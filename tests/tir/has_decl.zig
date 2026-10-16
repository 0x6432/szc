const N=struct {
	pub const @"\u{e9}":u8=256;
	const hidden:u8=256;
	pub fn bad() u8 { return 256; }
};
const bytes=[_]u8{0xc3,0xa9};
pub fn main() u8 {
	if(!@hasDecl(N,&bytes) or !@hasDecl(N,"bad")) return 1;
	if(@hasDecl(N,"hidden") or @hasDecl(N,"")) return 2;
	return 0;
}
