// exit: 0
// switch-families: 7,8,16,18
const Single=enum(u0){only};
pub fn main()u8 {
 const a:u8=switch(18446744073709551616){18446744073709551616=>1,else=>2};
 const b:u8=switch(@as(u128,18446744073709551616)){18446744073709551616=>3,else=>4};
 const c:u8=switch(@as(u0,0)){0=>5};
 const d:u8=switch(Single.only){.only=>|v| blk:{const same:Single=v;_=same;break :blk 6;}};
 const e:u8=switch(@as(u8,3)){3...3=>|v| blk:{const buf:[v]u8=undefined; _=&buf;break :blk 7;},else=>blk:{const bad:u8="bad";break :blk bad;}};
 if(a!=1 or b!=3 or c!=5 or d!=6 or e!=7)return 1;
 return 0;
}
