// exit: 0
const @"\x61nswer":u8=7;
const @"é":u8=11;
const @"_":u8=19;
var @"\x63ount":u8=1;
const N=struct {
	const @"\x53elf"=@This();
	const @"\x76alue":u8=23;
	var @"\x64ata":[3]u8=.{3,5,7};
	fn @"space name"(@"\x6e":u8) u8 { return n+1; }
};
const @"u8":u8=29;
var @"bool":u8=31;
pub fn @"\x6dain"() u8 {
	if(answer!=7 or @"answer"!=7 or @"\u{61}nswer"!=7) return 1;
	if(@"\u{e9}"!=11 or @"\xc3\xa9"!=11 or @"\x5f"!=19) return 2;
	count+=2;
	if(@"count"!=3) return 3;
	if(N.Self!=N or N.value!=23) return 4;
	N.data[1]=13;
	if(N.@"data".@"\x6cen"!=3 or N.data[1]!=13) return 5;
	const alias=N.@"space\x20name";
	const callback:*const fn(u8) u8=&N.@"space name";
	if(alias(5)!=6 or callback(9)!=10) return 6;
	const @"\x6cocal":u8=17;
	if(local!=17) return 7;
	if(@"u8"!=29 or @sizeOf(u8)!=1) return 8;
	@"bool"+=2;
	const p:*const type=&bool;
	if(p.*!=bool or @"bool"!=33) return 9;
	@"\x6cab": { break :lab; }
	var n:u8=0;
	@"loop": while(n<5):(n+=1) { if(n==3) break :@"\x6coop"; }
	if(n!=3) return 10;
	var total:usize=0;
	for(0..3) |@"\x69"| total+=i;
	if(total!=3) return 11;
	return 0;
}
