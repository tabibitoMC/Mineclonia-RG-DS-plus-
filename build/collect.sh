#!/bin/bash
set -e
OUT=/out/libs; mkdir -p $OUT /out/libs.fallback
SKIP='^(libc\.so|libm\.so|libdl\.so|libpthread\.so|ld-linux|librt\.so|libstdc\+\+|libgcc_s|libSDL2|libz\.so|libasound|libpulse|libGL|libEGL|libX|libxcb|libwayland|libdrm|libgbm|libdbus|libsystemd|libudev)'
find_lib() { for d in /src/deps-out/lib /usr/lib/aarch64-linux-gnu /lib/aarch64-linux-gnu; do [ -e "$d/$1" ] && { echo "$d/$1"; return; }; done; return 0; }
queue=$(aarch64-linux-gnu-readelf -d /src/build/bin/luanti | grep NEEDED | sed -n 's/.*\[\(.*\)\]/\1/p')
seen=""
while [ -n "$queue" ]; do
  next=""
  for l in $queue; do
    case " $seen " in *" $l "*) continue;; esac
    seen="$seen $l"
    echo "$l" | grep -qE "$SKIP" && continue
    p=$(find_lib "$l"); [ -z "$p" ] && { echo "NOTFOUND $l"; continue; }
    cp -L "$p" $OUT/$l
    next="$next $(aarch64-linux-gnu-readelf -d $p | grep NEEDED | sed -n 's/.*\[\(.*\)\]/\1/p')"
  done
  queue="$next"
done
cp -L /usr/lib/aarch64-linux-gnu/libstdc++.so.6 /out/libs.fallback/
cp -L /lib/aarch64-linux-gnu/libgcc_s.so.1 /out/libs.fallback/
aarch64-linux-gnu-strip --strip-unneeded $OUT/* /out/libs.fallback/* || true
cp /src/build/bin/luanti /out/luanti && aarch64-linux-gnu-strip /out/luanti
ls -la $OUT /out/libs.fallback /out/luanti
