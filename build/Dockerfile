FROM ubuntu:20.04
RUN echo 'Acquire::Retries "8";' > /etc/apt/apt.conf.d/80retries && \
  sed -i 's|^deb http|deb [arch=amd64] http|' /etc/apt/sources.list && \
  printf 'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal main universe\ndeb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports focal-updates main universe\n' > /etc/apt/sources.list.d/arm64.list && \
  dpkg --add-architecture arm64 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  g++-aarch64-linux-gnu gcc-aarch64-linux-gnu cmake ninja-build pkg-config file patchelf ca-certificates \
  libsdl2-dev:arm64 libfreetype-dev:arm64 libpng-dev:arm64 libjpeg-dev:arm64 zlib1g-dev:arm64 \
  libzstd-dev:arm64 libsqlite3-dev:arm64 libluajit-5.1-dev:arm64 libopenal-dev:arm64 \
  libvorbis-dev:arm64 libogg-dev:arm64 libgmp-dev:arm64 libjsoncpp-dev:arm64 \
  qemu-user-static && rm -rf /var/lib/apt/lists/*
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gcc g++ make libc6-dev && rm -rf /var/lib/apt/lists/*
