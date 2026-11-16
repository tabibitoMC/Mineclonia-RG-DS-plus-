# Compilar

Se compila en una PC con Docker (compilación cruzada a ARM64 con una glibc antigua,
para que el binario corra en el firmware de la consola).

```bash
# 1. Imagen con el compilador
docker build -t luanti-cross build/

# 2. Dependencias que se compilan aparte
mkdir -p deps
git clone --depth 1 -b v1.10.1 https://github.com/nih-at/libzip.git deps/libzip
git clone https://github.com/LuaJIT/LuaJIT.git deps/luajit

# 3. Compilar el motor y juntar las librerías
docker run --rm -v $PWD/engine:/src -v $PWD/deps:/deps -v $PWD/build/build.sh:/build.sh luanti-cross bash /build.sh
mkdir -p out
docker run --rm -v $PWD/engine:/src -v $PWD/out:/out -v $PWD/build/collect.sh:/collect.sh luanti-cross bash /collect.sh

# 4. Armar la carpeta para la SD (queda en dist/PORTS)
bash build/package.sh
```
