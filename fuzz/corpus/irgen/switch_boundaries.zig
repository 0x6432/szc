// exit: 0
// switch-families: 4,5,6,24
fn signed(x:i8)u8 { return switch(x){-128...-1=>1,0...127=>2}; }
fn tiny(x:u1)u8 {return switch(x){0=>3,1=>4};}
fn narrow(x:u7)u8 {return switch(x){0...126=>1,127=>2};}
fn medium(x:u9)u8 {return switch(x){0...510=>1,511=>2};}
fn wide(x:u33)u8 {return switch(x){0...8589934590=>1,8589934591=>2};}
fn huge(x:u64)u8 {return switch(x){0...9223372036854775807=>1,9223372036854775808...18446744073709551615=>2};}
fn full(x:u8)u8 {return switch(x){0...127=>1,128...255=>2};}
pub fn main()u8 {
 if(signed(-128)!=1 or signed(-1)!=1 or signed(0)!=2 or signed(127)!=2) return 1;
 if(tiny(0)!=3 or tiny(1)!=4 or narrow(127)!=2 or medium(511)!=2) return 2;
 if(wide(8589934591)!=2 or wide(0)!=1) return 3;
 if(huge(0)!=1 or huge(9223372036854775807)!=1 or huge(9223372036854775808)!=2 or huge(18446744073709551615)!=2) return 4;
 if(full(0)!=1 or full(255)!=2) return 5;
 return 0;
}
