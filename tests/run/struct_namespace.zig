// exit: 0
const Root=@This();
const Module=struct {
	const Self=@This();
	const Byte=u8;
	const root_check=root_scope;
	const len:usize=3;
	const value:Byte=17;
	var counter:Byte=1;
	var data:[3]Byte=.{3,5,7};
	var view:[]Byte=data[0..2];
	const aligned:Byte align(32)=23;
	const lazy_bad:Byte=256;
	fn add(n:Byte) Byte { counter+=n; return counter; }
	fn is_self() bool { return @This()==Self and Self==Module; }
	const Child=struct {
		const Self=@This();
		const value:Byte=29;
		fn is_self() bool { return @This()==@This().Self and @This().Self!=Module; }
	};
};
const Exported=struct {
	export fn szc_namespace_export(n:u8) u8 { return n+3; }
};
extern fn szc_namespace_export(n:u8) u8;
fn root_scope() bool { return @This()==Root; }
pub fn main() u8 {
	if(@This()!=Root or Module.Self!=Module or !Module.is_self()) return 1;
	if(!Module.Child.is_self() or Module.Child.value!=29 or Module.value!=17) return 2;
	if(Module.len!=3 or Module.counter!=1 or Module.add(4)!=5) return 3;
	const pointer=&Module.counter;
	if(pointer!=&Module.counter) return 4;
	pointer.*=11;
	if(Module.counter!=11 or Module.Self.counter!=11) return 5;
	const alias=Module.add;
	const callback:*const fn(u8) u8=&Module.add;
	if(alias(2)!=13 or callback(4)!=17) return 6;
	Module.data[1]=31;
	if(Module.data[1]!=31 or Module.view[1]!=31) return 7;
	Module.view.len=3;
	if(Module.view.len!=3 or Module.view[2]!=7) return 8;
	if(@intFromPtr(&Module.aligned)&31!=0 or Module.aligned!=23) return 9;
	const type_pointer=&Module;
	if(type_pointer.value!=17 or type_pointer.Child.value!=29) return 10;
	const byte:Module.Byte=37;
	if(byte!=37) return 11;
	{
		if(@This()!=Root) return 12;
	}
	_=Exported;
	if(szc_namespace_export(7)!=10) return 13;
	if(!Module.root_check()) return 14;
	const Local=struct {
		const Self=@This();
		fn check() bool { return @This()==Self and Self!=Root; }
	};
	if(!Local.check() or Local.Self==Module) return 15;
	return 0;
}
