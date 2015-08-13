#!/usr/bin/env sh
set -e

URL=http://libnf.net/packages/
DEP=libnf
VER=1.15
SUF=tar.gz

wget "${URL}${DEP}-${VER}.${SUF}"
tar -xf "${DEP}-${VER}.${SUF}"

cd "${DEP}-${VER}"
./configure --prefix="/usr" && make
sudo make install
