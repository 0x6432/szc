var calls:u8=0;
const T=@TypeOf(value());
fn value() u8 { calls+=1; return 7; }
fn query(a:u8,b:u16,p:*volatile u8) u16 {
	_=@TypeOf(p.*);
	_=@TypeOf({ p.*=99; });
	_=@TypeOf(value());
	_=@TypeOf(@as([2]u8,.{1,2}));
	const Peer=@TypeOf(a,b);
	const n:Peer=a;
	return n+b;
}
pub fn main() u8 {
	var x:T=3;
	const n=query(1,2,&x);
	if(n!=3 or x!=3 or calls!=0) return 1;
	return 0;
}
