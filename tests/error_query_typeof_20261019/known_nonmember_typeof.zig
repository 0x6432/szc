const Big=error{A,B};
const Small=error{A};
export fn probe()bool{
    const x:Big=error.B;
    return @TypeOf(@as(Small,@errorCast(x)))==Small;
}
pub fn main()void{}
