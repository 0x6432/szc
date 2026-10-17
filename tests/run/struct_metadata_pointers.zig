// exit: 0
// Synthetic addresses only. No dereference, load/store, or struct value exists.
const S=struct { a:u8, b:u64, c:u16, };
const W=struct { wide:u65, byte:u8, };
const Z=struct { marker:void, zero:u0, };
fn bytes(s:[]S) []u8 { return @ptrCast(s); }
fn structs(s:[]u8) []S { return @alignCast(@ptrCast(s)); }
fn wide_bytes(s:[]W) []u8 { return @ptrCast(s); }
fn zero_bytes(s:[]Z) []u8 { return @ptrCast(s); }
fn aligned(p:*align(1) S) *S { return @alignCast(p); }
fn verify(address:usize,count:usize) u8 {
	const p:[*]S=@ptrFromInt(address);
	if(@intFromPtr(p+1)!=address+16 or @intFromPtr(p+3)!=address+48) return 1;
	const wide:[*]W=@ptrFromInt(address);
	if(@intFromPtr(wide+1)!=address+32) return 2;
	const zero:[*]Z=@ptrFromInt(address);
	if(@intFromPtr(zero+19)!=address) return 3;
	const array:*[3]S=@ptrFromInt(address);
	const s:[]S=array;
	if(s.len!=3 or @intFromPtr(s.ptr)!=address) return 4;
	const dynamic=p[0..count];
	const b=bytes(dynamic);
	if(b.len!=count*@sizeOf(S) or @intFromPtr(b.ptr)!=address) return 5;
	const again=structs(b);
	if(again.len!=count or @intFromPtr(again.ptr+2)!=address+32) return 6;
	const wb=wide_bytes(wide[0..3]);
	if(wb.len!=96 or @intFromPtr(wb.ptr)!=address) return 7;
	const zb=zero_bytes(zero[0..5]);
	if(zb.len!=0 or @intFromPtr(zb.ptr)!=address) return 8;
	const unaligned:*align(1) S=@ptrFromInt(address);
	if(@intFromPtr(aligned(unaligned))!=address) return 9;
	const q:[*]S=@ptrCast(s);
	if(@intFromPtr(q+1)!=address+16) return 10;
	return 0;
}
pub fn main() u8 { return verify(4096,3); }
