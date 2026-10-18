pub fn main()u8{
const T=u8;const n:T=7;
const S=struct{x:T=n,p:*const T=&n};
const s:S=.{};return s.x+s.p.*;
}
