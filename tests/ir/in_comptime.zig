const T=@TypeOf(@inComptime());
fn query(a:bool) bool { return a or @inComptime(); }
pub fn main() u8 {
	const runtime=@inComptime();
	if(runtime or query(false) or T!=bool) return 1;
	return 0;
}
