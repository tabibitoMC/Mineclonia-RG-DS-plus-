#!/bin/bash
# Arma dist/PORTS (lo que se copia a Roms/PORTS) a partir de out/, engine/, mineclonia/ y port/
set -e
cd "$(dirname "$0")/.."
OUT=dist/PORTS; L=$OUT/luanti
rm -rf dist && mkdir -p $L/bin $L/games $L/worlds $L/mods $L/textures $L/client
cp out/luanti $L/bin/
cp -r out/libs out/libs.fallback $L/
cp -r engine/builtin engine/fonts $L/
cp -r engine/client/shaders $L/client/
cp -r engine/textures/base $L/textures/
cp port/assets/textures/*.png $L/textures/base/pack/
cp port/assets/fonts/* $L/fonts/
cp -r mineclonia $L/games/mineclonia
cp port/minetest.conf port/LEEME.txt $L/
cp engine/LICENSE.txt $L/LICENSE-luanti.txt
cp README.md CREDITS.md CHANGELOG.md $L/
cp port/Luanti.sh $OUT/ && chmod +x $OUT/Luanti.sh
(cd dist && zip -qr -9 ../Mineclonia-RGDSPlus.zip PORTS)
echo "Listo: Mineclonia-RGDSPlus.zip"
