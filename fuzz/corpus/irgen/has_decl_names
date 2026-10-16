// exit: 0
const Root=@This();
const N=struct {
	pub const Self=@This();
	pub const bad_value:u8=256;
	pub var counter:u8=3;
	pub const Byte=u8;
	pub const len:usize=1;
	pub const ptr:u8=7;
	pub const @"space name":u8=1;
	pub const @"\u{e9}":u8=1;
	pub const @"\xff":u8=1;
	pub const @"u8":u8=1;
	const private_value:u8=256;
	pub fn bad_function() u8 { return 256; }
	pub const Child=struct { pub const own:u8=3; };
	pub fn private_check() bool { return @hasDecl(@This(),"private_value"); }
};
const bytes=[_]u8{'b','a','d','_','v','a','l','u','e'};
pub fn main() u8 {
	if(!@hasDecl(N,"Self") or !@hasDecl(N,"counter") or !@hasDecl(N,"Byte")) return 1;
	if(!@hasDecl(N,"bad_value") or !@hasDecl(N,"bad_function")) return 2;
	if(@hasDecl(N,"private_value") or N.private_check()) return 3;
	if(@hasDecl(N,"missing") or @hasDecl(N,"") or @hasDecl(N,"bad_value\x00suffix")) return 4;
	if(!@hasDecl(N,"space\x20name") or !@hasDecl(N,"\xc3\xa9") or !@hasDecl(N,"\xff")) return 5;
	if(!@hasDecl(N,"u8") or !@hasDecl(N,"len") or !@hasDecl(N,"ptr")) return 6;
	if(!@hasDecl(N.Child,"own") or @hasDecl(N.Child,"counter")) return 7;
	if(!@hasDecl(Root,"main") or @hasDecl(Root,"N")) return 8;
	if(!@hasDecl(N,&bytes) or !@hasDecl(N,"bad_"++"value")) return 9;
	const name:[]const u8=".bad_value."[1..10];
	if(!@hasDecl(N,name)) return 10;
	const Alias=N;
	const Local=struct { pub const bad:u8=256; };
	if(!@hasDecl(Alias,"Child") or !@hasDecl(Local,"bad")) return 11;
	const empty=[_]u8{};
	if(@hasDecl(N,&empty) or @hasDecl(N,"\x00counter")) return 12;
	if(!@hasDecl(N,blk:{ break :blk "counter"; })) return 13;
	if(!@hasDecl(N,&[_:0]u8{'l','e','n'}) or !@hasDecl(N,&.{'p','t','r'})) return 14;
	N.counter+=1;
	if(N.counter!=4) return 15;
	return 0;
}
