// exit: 0
const Query=@TypeOf(blk: {
	const n:u8=if(!@inComptime())0 else 256;
	_=n;
	break :blk false;
});
fn mode() bool { return @inComptime(); }
inline fn inline_mode() bool { return @inComptime(); }
fn choose(a:u8) u8 {
	if(@inComptime()) return 99;
	const T=if(@inComptime())u16 else u8;
	const n:T=a;
	return n;
}
pub fn main() u8 {
	if(@inComptime() or mode() or inline_mode()) return 1;
	if(Query!=bool or @TypeOf(@inComptime())!=bool) return 2;
	const T=if(@inComptime())u8 else u16;
	if(T!=u16 or choose(7)!=7) return 3;
	const n:u8=if(@inComptime())256 else 0;
	_=n;
	const values=[_]bool{@inComptime(),!@inComptime()};
	if(values[0] or !values[1]) return 4;
	var bits=[_]bool{true,false};
	const view:[:false]bool=bits[0..1:@inComptime()];
	if(view.len!=1 or !view[0] or view.ptr[1]) return 5;
	const Again=@TypeOf(blk: {
		const answer:u8=if(!@inComptime())0 else 256;
		_=answer;
		break :blk @as(u8,1);
	});
	if(Again!=u8 or @inComptime()) return 6;
	return 0;
}
