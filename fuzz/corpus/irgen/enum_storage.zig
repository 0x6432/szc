const E=enum(u33){lo=1,hi=4294967297};const S=struct{e:E=.hi,n:u8=7};var g:[2]S=.{.{},.{.e=.lo}};
noinline fn index()usize{return 1;}
pub fn main()u8{g[index()].e=.hi;return if(g[1].e==.hi)0 else 1;}
