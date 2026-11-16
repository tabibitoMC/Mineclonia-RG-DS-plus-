#!/bin/bash
# Luanti + Mineclonia para Anbernic RG DS Plus (firmware stock Linux)
# Va en Roms/PORTS (o Roms/ports). La carpeta "luanti" va al lado de este archivo.

DIR="$(cd "$(dirname "$0")" && pwd)/luanti"
LOG="$DIR/log.txt"
cd "$DIR" || exit 1

{
echo "===== Luanti RG DS Plus $(date) ====="
echo "--- uname"; uname -a
echo "--- libc"; ldd --version 2>&1 | head -1
echo "--- memoria"; free -m 2>&1
echo "--- DRM"; ls -l /dev/dri 2>&1; ls /sys/class/drm 2>&1
echo "--- entrada"; cat /proc/bus/input/devices 2>/dev/null | grep -E "^N:|^H:"
echo "--- pantallas/env"; env | grep -iE "DISPLAY|WAYLAND|SDL|XDG" 2>&1
echo "--- procesos graficos"; ps 2>/dev/null | grep -iE "weston|sway|Xorg|retroarch|frontend|launcher" | grep -v grep
} > "$LOG" 2>&1

# Librerias propias primero (las del sistema para SDL2, GL y audio)
export LD_LIBRARY_PATH="$DIR/libs:$LD_LIBRARY_PATH"
# Si el libstdc++ del sistema es muy viejo, usar el incluido
SYSCXX=""
for f in /usr/lib/aarch64-linux-gnu/libstdc++.so.6 /usr/lib64/libstdc++.so.6 /usr/lib/libstdc++.so.6 /lib/libstdc++.so.6; do
  [ -e "$f" ] && { SYSCXX="$f"; break; }
done
if [ -z "$SYSCXX" ] || ! grep -q "GLIBCXX_3.4.26" "$SYSCXX" 2>/dev/null; then
  export LD_LIBRARY_PATH="$DIR/libs.fallback:$LD_LIBRARY_PATH"
  echo "usando libstdc++ incluido (sistema: ${SYSCXX:-ninguno})" >> "$LOG"
fi

# Mapeo del mando de la RG DS Plus por posicion fisica de los botones
# (X arriba, B abajo, Y izquierda, A derecha; SELECT = "back", MENU = "guide").
# En esta consola: B=b0, A=b1, X=b2, Y=b3. SDL nombra por posicion:
# a=abajo(B), b=derecha(A), x=izquierda(Y), y=arriba(X).
RGDS_MAP="a:b0,b:b1,x:b3,y:b2,back:b6,guide:b8,start:b7,leftstick:b9,rightstick:b12,leftshoulder:b4,rightshoulder:b5,dpup:h0.1,dpleft:h0.8,dpdown:h0.4,dpright:h0.2,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:b10,righttrigger:b11,platform:Linux,"
unset SDL_GAMECONTROLLERCONFIG_FILE
export SDL_GAMECONTROLLERCONFIG="1900b655010000000100000000010000,ANBERNIC-rk3568-keys,$RGDS_MAP
19000000010000000100000000010000,ANBERNIC-rk3568-keys,$RGDS_MAP"

# Pantallas y tactil: el menu de la consola apaga el tactil (tpctrl=1) al abrir
# programas externos. 0 = tactil encendido. Al salir se deja como estaba.
export SDL_TOUCH_MOUSE_EVENTS=0
export SDL_MOUSE_TOUCH_EVENTS=0
MISC=/sys/class/anbernic_misc
if [ "$(cat $MISC/lcdoff 2>/dev/null)" != "0" ] && [ -w $MISC/lcdoff ]; then
  echo 0 > $MISC/lcdoff
  [ -x /mnt/vendor/bin/bright.dge ] && /mnt/vendor/bin/bright.dge >/dev/null 2>&1
fi
OLD_TP="$(cat $MISC/tpctrl 2>/dev/null)"
[ -w $MISC/tpctrl ] && echo 0 > $MISC/tpctrl
echo "tactil: tpctrl antes=$OLD_TP ahora=$(cat $MISC/tpctrl 2>/dev/null)" >> "$LOG"

# Doble pantalla: 1 = activada, 0 = solo pantalla de arriba.
# Si salen al reves, cambia TOP=1 y BOTTOM=0.
export LUANTI_DUALSCREEN=1
export LUANTI_TOP_DISPLAY=0
export LUANTI_BOTTOM_DISPLAY=1

# Pantalla tactil de abajo (se lee directo del sistema).
# Si al tocar el punto sale en otro lugar, prueba con estas opciones:
#   LUANTI_TOUCH_ROTATE=90 (o 180 / 270), LUANTI_TOUCH_SWAPXY=1,
#   LUANTI_TOUCH_INVERTX=1, LUANTI_TOUCH_INVERTY=1
export LUANTI_TOUCH_ROTATE=0
echo "--- dispositivos de entrada con permisos" >> "$LOG"
ls -l /dev/input/ >> "$LOG" 2>&1

# ---- Memoria extra: swap comprimida en RAM (zram) o archivo swap en la SD ----
SWAPDEV=""
if [ "$(wc -l < /proc/swaps)" -le 1 ]; then
  if [ ! -e /sys/block/zram0 ]; then modprobe zram num_devices=1 >> "$LOG" 2>&1; fi
  if [ -e /sys/block/zram0 ] && [ "$(cat /sys/block/zram0/disksize 2>/dev/null)" = "0" ]; then
    echo lz4 > /sys/block/zram0/comp_algorithm 2>/dev/null
    if echo 640M > /sys/block/zram0/disksize 2>>"$LOG" && mkswap /dev/zram0 >> "$LOG" 2>&1 \
       && swapon -p 100 /dev/zram0 >> "$LOG" 2>&1; then
      SWAPDEV=/dev/zram0
      echo "zram activado (640M)" >> "$LOG"
    fi
  fi
  if [ -z "$SWAPDEV" ]; then
    SWF="$DIR/swapfile"
    if [ ! -f "$SWF" ]; then
      echo "creando swapfile 512M (solo la primera vez)" >> "$LOG"
      dd if=/dev/zero of="$SWF" bs=1M count=512 >> "$LOG" 2>&1
      chmod 600 "$SWF"
      mkswap "$SWF" >> "$LOG" 2>&1
    fi
    if swapon "$SWF" >> "$LOG" 2>&1; then
      SWAPDEV="$SWF"
      echo "swapfile activado" >> "$LOG"
    else
      echo "no se pudo activar swap" >> "$LOG"
    fi
  fi
fi
echo 60 > /proc/sys/vm/swappiness 2>/dev/null
echo "--- memoria con swap"  >> "$LOG"; free -m >> "$LOG" 2>&1

# Memoria: menos fragmentacion y devolver al sistema lo que se libera
export MALLOC_ARENA_MAX=2
export MALLOC_TRIM_THRESHOLD_=4194304
export MALLOC_MMAP_THRESHOLD_=131072

echo "--- arrancando" >> "$LOG"
./bin/luanti --config "$DIR/minetest.conf" --logfile "$DIR/debug.txt" >> "$LOG" 2>&1
CODE=$?
echo "--- salio con codigo $CODE (137 = sin memoria)" >> "$LOG"
dmesg 2>/dev/null | grep -iE "out of memory|oom|killed process" | tail -5 >> "$LOG"
[ -n "$OLD_TP" ] && [ -w $MISC/tpctrl ] && echo "$OLD_TP" > $MISC/tpctrl
if [ -n "$SWAPDEV" ]; then
  swapoff "$SWAPDEV" >> "$LOG" 2>&1
  [ "$SWAPDEV" = "/dev/zram0" ] && echo 1 > /sys/block/zram0/reset 2>/dev/null
fi
sync
