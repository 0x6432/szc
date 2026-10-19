// exit: 0
// switch-families: 23
const S=struct{a:u8,b:u8};
fn arrays(x:u8)u8 {
 var a:[2]u8=.{3,4};var b:[2]u8=.{5,6};var c:[2]u8=.{7,8};
 const saved:[2]u8=switch(x){
 0=>blk:{defer a[0]=99;break :blk a;},
 1=>blk:{defer b[0]=99;break :blk b;},
 else=>blk:{defer c[0]=99;break :blk c;},
 };
 if(x==0 and a[0]!=99)return 100;if(x==1 and b[0]!=99)return 101;if(x==2 and c[0]!=99)return 102;
 return saved[0]+saved[1];
}
fn structs(x:u8)u8 {var a:S=.{.a=3,.b=4};const s:S=switch(x){0=>blk:{defer a.a=90;break :blk a;},1=>.{.a=5,.b=6},else=>.{.a=7,.b=8}};return s.a+s.b;}
fn slices(x:u8)u8 {var a:[2]u8=.{3,4};var b:[2]u8=.{7,8};var view:[]u8=&a;
 const s:[]u8=switch(x){0=>blk:{defer view=&b;break :blk view;},else=>&b};
 if(x==0 and view[0]!=7)return 100;return s[0]+s[1];}
pub fn main()u8 {if(arrays(0)!=7 or arrays(1)!=11 or arrays(2)!=15)return 1;if(structs(0)!=7 or structs(1)!=11 or structs(2)!=15)return 2;if(slices(0)!=7 or slices(1)!=15)return 3;return 0;}
