fn f() !void {
    errdefer |e| log(e);
}
