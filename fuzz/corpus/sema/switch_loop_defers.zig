// exit: 0
// switch-families: 23
fn f()u16 {var i:u8=0;var n:u16=0;while(i<5):(i+=1){defer n+=10;switch(i){0=>{defer n+=1;continue;},1=>{defer n+=2;},2=>{defer n+=3;break;},else=>{n+=99;}}n+=100;}return n;}
fn labels(x:u8)u8 {var n:u8=0;const r:u8=switch(x){0=>blk:{defer n+=1;break :blk 7;},else=>blk:{defer n+=2;break :blk 9;}};return r+n;}
pub fn main()u8 {if(f()!=136)return 1;if(labels(0)!=8 or labels(1)!=11)return 2;return 0;}
