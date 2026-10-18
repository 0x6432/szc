// exit: 0
const E=enum(u8){a=1,b=7,c=250,};
var counter:u32=0;
var global:E=.a;
noinline fn decode(n:u16)E{counter+=1;return @enumFromInt(n);}
noinline fn first(e:E,later:u8)E{_=later;return e;}
noinline fn change()u8{global=.c;return 0;}
noinline fn same(e:E)E{return e;}
noinline fn other(e:E)E{_=e;return .c;}
noinline fn call(p:*const fn(E)E,e:E)E{return p(e);}
const Holder=struct{e:E=.b,cb:*const fn(E)E=&same,};
var holder:Holder=.{};
noinline fn changeCallback()E{holder.cb=&other;return .a;}
noinline fn take(h:Holder,later:u8)Holder{_=later;return h;}
noinline fn changeHolder()u8{holder.e=.c;return 0;}
noinline fn returnSnapshot(p:*Holder)Holder{defer p.e=.a;return p.*;}
noinline fn recurse(n:u8,e:E)E{if(n==0)return e;return recurse(n-1,e);}
pub fn main()u8{
 const e=decode(7);if(e!=.b or counter!=1)return 1;
 var a:[2]E=.{decode(1),decode(250)};if(a[0]!=.a or a[1]!=.c or counter!=3)return 2;
 a[0]=decode(7);if(a[0]!=.b or counter!=4)return 3;
 if(first(global,change())!=.a or global!=.c)return 4;
 if(call(&same,.b)!=.b or call(&other,.a)!=.c)return 5;
 if(holder.cb(changeCallback())!=.a or holder.cb(.a)!=.c)return 6;
 holder.e=.b;const snapshot=take(holder,changeHolder());if(snapshot.e!=.b or holder.e!=.c)return 7;
 holder.e=.c;const returned=returnSnapshot(&holder);if(returned.e!=.c or holder.e!=.a)return 8;
 if(recurse(4,.b)!=.b)return 9;
 const callback:*const fn(E)E=&same;const r=callback(decode(250));if(r!=.c or counter!=5)return 10;
 return 0;
}
