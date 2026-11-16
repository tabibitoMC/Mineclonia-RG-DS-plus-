#!/bin/bash
set -e
cd /src
sed -i "s#set(SDL2_LIBRARIES \" -lSDL2\")#set(SDL2_LIBRARIES \"/usr/lib/aarch64-linux-gnu/libSDL2.so\")#" /usr/lib/aarch64-linux-gnu/cmake/SDL2/sdl2-config.cmake
mkdir -p build && cd build
cat > tc.cmake <<'TC'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)
set(ENV{PKG_CONFIG_LIBDIR} /usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig)
TC
if [ ! -f /src/deps-out/lib/libzip.so ]; then
  mkdir -p /tmp/zb && cd /tmp/zb
  cmake /deps/libzip -G Ninja -DCMAKE_TOOLCHAIN_FILE=/src/build/tc.cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/src/deps-out -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=ON \
    -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF \
    -DENABLE_OPENSSL=OFF -DENABLE_COMMONCRYPTO=OFF -DENABLE_WINDOWS_CRYPTO=OFF \
    -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF -DBUILD_OSSFUZZ=OFF > /tmp/zb.log 2>&1 || { tail -20 /tmp/zb.log; exit 1; }
  ninja install > /tmp/zbi.log 2>&1 || { tail -20 /tmp/zbi.log; exit 1; }
  cd /src/build
fi
if [ ! -f /src/deps-out/lib/libluajit-5.1.a ]; then
  rm -rf /tmp/lj && cp -r /deps/luajit /tmp/lj && cd /tmp/lj
  make -j3 HOST_CC=gcc CROSS=aarch64-linux-gnu- TARGET_SYS=Linux TARGET_CFLAGS="-mcpu=cortex-a55" BUILDMODE=static amalg > /tmp/lj.log 2>&1 || { tail -20 /tmp/lj.log; exit 1; }
  make install PREFIX=/src/deps-out > /tmp/lji.log 2>&1 || { tail -20 /tmp/lji.log; exit 1; }
  cd /src/build
fi
cmake .. -G Ninja -DLUA_INCLUDE_DIR=/src/deps-out/include/luajit-2.1 -DLUA_LIBRARY=/src/deps-out/lib/libluajit-5.1.a -DLIBZIP_INCLUDE_DIR=/src/deps-out/include -DLIBZIP_LIBRARY=/src/deps-out/lib/libzip.so -DCMAKE_TOOLCHAIN_FILE=tc.cmake -DCMAKE_BUILD_TYPE=Release \
  -DRUN_IN_PLACE=TRUE -DBUILD_SERVER=FALSE -DBUILD_CLIENT=TRUE -DBUILD_UNITTESTS=FALSE \
  -DENABLE_OPENGL=OFF -DENABLE_OPENGL3=OFF -DENABLE_GLES2=ON \
  -DENABLE_CURL=OFF -DENABLE_GETTEXT=OFF -DENABLE_LEVELDB=OFF -DENABLE_REDIS=OFF \
  -DENABLE_POSTGRESQL=OFF -DENABLE_SPATIAL=OFF -DENABLE_PROMETHEUS=OFF -DENABLE_UPDATE_CHECKER=OFF \
  -DENABLE_SOUND=ON -DENABLE_LUAJIT=ON -DENABLE_SYSTEM_JSONCPP=OFF -DENABLE_SYSTEM_GMP=OFF \
  -DCMAKE_CXX_FLAGS="-mcpu=cortex-a55 -O2" -DCMAKE_C_FLAGS="-mcpu=cortex-a55 -O2" 2>&1 | tail -25
ninja -j3 > /src/build/ninja.log 2>&1 || { grep -E "error|Error|FAILED" -A5 /src/build/ninja.log | head -60; exit 1; }
