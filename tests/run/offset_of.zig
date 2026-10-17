// exit: 0
// Numeric offsets captured from official ELF exports, never modeled by sorting.
const Inner=struct { byte:u8, word:u32, };
pub fn main() u8 {
	{ // perm3/0
		const S=struct {f0:u8,f1:u64,f2:u16,};
		if(@offsetOf(S,"f0")!=10 or @offsetOf(S,"f1")!=0 or @offsetOf(S,"f2")!=8) return 1;
	}
	{ // zero/0
		const S=struct {a:u8,z:void,empty:[0]u64,bits:u0,b:u64,};
		if(@offsetOf(S,"a")!=8 or @offsetOf(S,"z")!=9 or @offsetOf(S,"empty")!=0 or @offsetOf(S,"bits")!=9 or @offsetOf(S,"b")!=0) return 2;
	}
	{ // all-zero
		const S=struct {v:void,u:u0,a:[0]u64,};
		if(@offsetOf(S,"v")!=0 or @offsetOf(S,"u")!=0 or @offsetOf(S,"a")!=0) return 3;
	}
	{ // wide/0
		const S=struct {a:u513,b:i129,c:u24,d:u33,e:[3]u8,f:bool,};
		if(@offsetOf(S,"a")!=0 or @offsetOf(S,"b")!=80 or @offsetOf(S,"c")!=120 or @offsetOf(S,"d")!=112 or @offsetOf(S,"e")!=124 or @offsetOf(S,"f")!=127) return 4;
	}
	{ // nested
		const S=struct {a:u8,inner:Inner,array:[3]Inner,matrix:[2][3]u16,z:void,};
		if(@offsetOf(S,"a")!=44 or @offsetOf(S,"inner")!=0 or @offsetOf(S,"array")!=8 or @offsetOf(S,"matrix")!=32 or @offsetOf(S,"z")!=45) return 5;
	}
	{ // self-pointer
		const S=struct {tag:u8,next:*const @This(),children:[]@This(),};
		if(@offsetOf(S,"tag")!=24 or @offsetOf(S,"next")!=0 or @offsetOf(S,"children")!=8) return 6;
	}
	{ // many/64/shuffle
		const S=struct {f34:u16,f62:u24,f26:u16,f1:u64,f7:bool,f56:u8,f63:bool,f60:void,f28:void,f44:void,f50:u16,f5:[0]u32,f33:u64,f51:u65,f38:u24,f39:bool,f6:u24,f16:u8,f9:u64,f30:u24,f57:u64,f61:[0]u32,f36:void,f25:u64,f15:bool,f53:[0]u32,f58:u16,f21:[0]u32,f41:u64,f11:u65,f27:u65,f59:u65,f18:u16,f48:u8,f20:void,f22:u24,f37:[0]u32,f17:u64,f2:u16,f0:u8,f45:[0]u32,f42:u16,f54:u24,f10:u16,f32:u8,f24:u8,f35:u65,f12:void,f8:u8,f23:bool,f40:u8,f3:u65,f55:bool,f52:void,f29:[0]u32,f4:void,f14:u24,f43:u65,f46:u24,f47:bool,f49:u64,f13:[0]u32,f31:bool,f19:u65,};
		if(@offsetOf(S,"f34")!=228 or @offsetOf(S,"f62")!=220 or @offsetOf(S,"f26")!=232 or @offsetOf(S,"f1")!=168 or @offsetOf(S,"f7")!=255 or @offsetOf(S,"f56")!=253 or @offsetOf(S,"f63")!=252 or @offsetOf(S,"f60")!=251 or @offsetOf(S,"f28")!=251 or @offsetOf(S,"f44")!=251 or @offsetOf(S,"f50")!=230 or @offsetOf(S,"f5")!=204 or @offsetOf(S,"f33")!=136 or @offsetOf(S,"f51")!=96 or @offsetOf(S,"f38")!=204 or @offsetOf(S,"f39")!=240 or @offsetOf(S,"f6")!=208 or @offsetOf(S,"f16")!=250 or @offsetOf(S,"f9")!=152 or @offsetOf(S,"f30")!=212 or @offsetOf(S,"f57")!=128 or @offsetOf(S,"f61")!=220 or @offsetOf(S,"f36")!=249 or @offsetOf(S,"f25")!=144 or @offsetOf(S,"f15")!=245 or @offsetOf(S,"f53")!=220 or @offsetOf(S,"f58")!=236 or @offsetOf(S,"f21")!=220 or @offsetOf(S,"f41")!=176 or @offsetOf(S,"f11")!=32 or @offsetOf(S,"f27")!=112 or @offsetOf(S,"f59")!=16 or @offsetOf(S,"f18")!=224 or @offsetOf(S,"f48")!=242 or @offsetOf(S,"f20")!=242 or @offsetOf(S,"f22")!=192 or @offsetOf(S,"f37")!=220 or @offsetOf(S,"f17")!=184 or @offsetOf(S,"f2")!=234 or @offsetOf(S,"f0")!=241 or @offsetOf(S,"f45")!=220 or @offsetOf(S,"f42")!=238 or @offsetOf(S,"f54")!=216 or @offsetOf(S,"f10")!=226 or @offsetOf(S,"f32")!=243 or @offsetOf(S,"f24")!=244 or @offsetOf(S,"f35")!=48 or @offsetOf(S,"f12")!=246 or @offsetOf(S,"f8")!=246 or @offsetOf(S,"f23")!=247 or @offsetOf(S,"f40")!=248 or @offsetOf(S,"f3")!=80 or @offsetOf(S,"f55")!=249 or @offsetOf(S,"f52")!=250 or @offsetOf(S,"f29")!=212 or @offsetOf(S,"f4")!=251 or @offsetOf(S,"f14")!=200 or @offsetOf(S,"f43")!=0 or @offsetOf(S,"f46")!=196 or @offsetOf(S,"f47")!=251 or @offsetOf(S,"f49")!=160 or @offsetOf(S,"f13")!=220 or @offsetOf(S,"f31")!=254 or @offsetOf(S,"f19")!=64) return 7;
	}
	if(@TypeOf(@offsetOf(Inner,"word"))!=comptime_int) return 8;
	{ // supported u32 struct-size boundary; metadata only, no giant value
		const S=struct { a:[4294967294]u8, b:u8, };
		if(@sizeOf(S)!=4294967295 or @offsetOf(S,"b")!=4294967294) return 9;
	}
	return 0;
}
