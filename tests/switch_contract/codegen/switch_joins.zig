// exit: 0
// switch-families: 20,21,22
const E=enum{a,b,c};
fn ascending(x:u8)u32 {const r=switch(x){0=>@as(u8,7),1=>@as(u16,300),else=>@as(u32,70000)};return r;}
fn descending(x:u8)u32 {const r=switch(x){0=>@as(u32,70000),1=>@as(u16,300),else=>@as(u8,7)};return r;}
fn contextual(x:u8)E {return switch(x){0=>.a,1=>.b,else=>.c};}
fn one_live(x:u8,p:*u8)u8 {const r=switch(x){0=>return 10,1=>return 11,else=>blk:{p.*+=1;break :blk @as(u8,7);}};return r;}
fn all_return(x:u8)u8 {return switch(x){0=>return 20,1=>return 21,else=>return 22};}
fn voids(x:u8,p:*u8)void {switch(x){0=>{p.*+=1;},1=>{p.*+=2;},else=>{p.*+=3;}}}
fn perm0(x:u8)u32{const r=switch(x){0=>@as(u8,7),1=>@as(u16,300),else=>@as(u32,70000)};return r;}
fn perm1(x:u8)u32{const r=switch(x){0=>@as(u8,7),1=>@as(u32,70000),else=>@as(u16,300)};return r;}
fn perm2(x:u8)u32{const r=switch(x){0=>@as(u16,300),1=>@as(u8,7),else=>@as(u32,70000)};return r;}
fn perm3(x:u8)u32{const r=switch(x){0=>@as(u16,300),1=>@as(u32,70000),else=>@as(u8,7)};return r;}
fn perm4(x:u8)u32{const r=switch(x){0=>@as(u32,70000),1=>@as(u8,7),else=>@as(u16,300)};return r;}
fn perm5(x:u8)u32{const r=switch(x){0=>@as(u32,70000),1=>@as(u16,300),else=>@as(u8,7)};return r;}
fn known_survivor(x:u8,p:*u8)u8{const r=switch(x){0=>return 10,1=>return 11,else=>blk:{p.*+=1;break :blk 7;}};const a:[r]u8=undefined;_=&a;return r;}
pub fn main()u8 {
 if(ascending(0)!=7 or ascending(1)!=300 or ascending(2)!=70000)return 1;
 if(descending(0)!=70000 or descending(1)!=300 or descending(2)!=7)return 2;
 if(contextual(0)!=.a or contextual(1)!=.b or contextual(2)!=.c)return 3;
 if(perm0(0)!=7 or perm0(1)!=300 or perm0(2)!=70000)return 10;
 if(perm1(0)!=7 or perm1(1)!=70000 or perm1(2)!=300)return 11;
 if(perm2(0)!=300 or perm2(1)!=7 or perm2(2)!=70000)return 12;
 if(perm3(0)!=300 or perm3(1)!=70000 or perm3(2)!=7)return 13;
 if(perm4(0)!=70000 or perm4(1)!=7 or perm4(2)!=300)return 14;
 if(perm5(0)!=70000 or perm5(1)!=300 or perm5(2)!=7)return 15;
 var n:u8=0;if(one_live(0,&n)!=10 or one_live(1,&n)!=11 or one_live(2,&n)!=7 or n!=1)return 4;
 if(all_return(0)!=20 or all_return(1)!=21 or all_return(2)!=22)return 5;
 voids(0,&n);voids(1,&n);voids(2,&n);if(n!=7)return 6;if(known_survivor(0,&n)!=10 or known_survivor(1,&n)!=11 or known_survivor(2,&n)!=7 or n!=8)return 20;return 0;
}
