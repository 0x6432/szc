comptime { const n=@errorName(error.@"a\x22\x5c\n\xff"); if(n.len!=5 or n[0]!=97 or n[1]!=34 or n[2]!=92 or n[3]!=10 or n[4]!=255) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
