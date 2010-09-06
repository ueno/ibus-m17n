#!/bin/sh
set -e
set -x

autopoint --force
libtoolize --automake --copy
intltoolize --copy --force
aclocal -I m4
autoheader
automake --add-missing --copy
autoconf
export CFLAGS="-g -O0"
export CXXFLAGS="$CFLAGS"
./configure --enable-maintainer-mode $*
