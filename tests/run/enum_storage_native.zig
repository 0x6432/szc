// exit: 0
const E=enum(u9){a=1,b=257,c=511,};
const I=struct{e:E=.b,};
const H=struct{i:I=.{},n:u8=3,};
var global:E=.a;
var objects:[2]H=.{.{},.{.i=.{.e=.c},.n=9}};
noinline fn decode(n:u16)E{return @enumFromInt(n);}
noinline fn index()usize{return 1;}
noinline fn get(p:*const E)E{return p.*;}
noinline fn put(p:*E,e:E)void{p.*=e;}
noinline fn identity(h:H)H{return h;}
noinline fn identityArray(a:[2]E)[2]E{return a;}
noinline fn mutateSlice(s:[]E)void{s[1]=.c;}
noinline fn volatilePut(p:*volatile E,e:E)void{p.*=e;}
noinline fn volatileGet(p:*volatile E)E{return p.*;}
pub fn main()u8{
 if(get(&global)!=.a)return 1;
 put(&global,decode(257));if(global!=.b)return 2;
 var local:H=.{};if(local.i.e!=.b or local.n!=3)return 3;
 local.i.e=decode(511);const copy=identity(local);local.i.e=.a;if(copy.i.e!=.c)return 4;
 var a:[2]E=.{.a,.b};a[index()]=decode(511);if(a[1]!=.c)return 5;
 mutateSlice(&a);if(a[0]!=.a or a[1]!=.c)return 6;
 const aa=identityArray(a);a[0]=.b;if(aa[0]!=.a)return 7;
 objects[index()].i.e=decode(257);if(objects[1].i.e!=.b or objects[0].i.e!=.b)return 8;
 volatilePut(&global,.c);if(volatileGet(&global)!=.c)return 9;
 const p:*E=&objects[1].i.e;put(p,.a);if(objects[1].i.e!=.a or @intFromPtr(p)%@alignOf(E)!=0)return 10;
 return 0;
}
