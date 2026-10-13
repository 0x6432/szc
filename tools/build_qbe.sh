#!/bin/sh
# QBE stays an independent executable, pinned for reproducible compiler tests.
set -eu
dir=${1:-build/qbe-src}
revision=e786f06032fefa2e3790d6b1c9e31ed138f475a6
if [ ! -d "$dir/.git" ]; then
	git clone git://c9x.me/qbe.git "$dir"
fi
git -C "$dir" checkout --detach "$revision"
make -C "$dir" CC="${CC:-cc}"
