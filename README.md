# Mineclonia para la Anbernic RG DS Plus

Port de [Mineclonia](https://codeberg.org/mineclonia/mineclonia) (sobre el motor
[Luanti](https://www.luanti.org/)) adaptado a las dos pantallas de la **Anbernic RG DS Plus**
con su firmware Linux de fábrica.

> **Proyecto no oficial.** No tiene relación con el equipo de Mineclonia, con Luanti,
> con Anbernic, ni con Mojang Studios o Microsoft.

## Qué hace

- **Pantalla de arriba:** el mundo.
- **Pantalla de abajo (táctil):** barra de objetos, coordenadas, zona para mover la cámara
  con el dedo y botones de inventario, cámara y pausa. El inventario, los cofres y los
  menús también se abren abajo y se manejan tocando.
- **Botones:** en el juego, X/B/Y/A mueven la cámara (la consola no tiene stick derecho),
  R1 pica, L1 construye. En los menús, A acepta y B vuelve.
- Ajustes pensados para 1 GB de RAM.

## Qué NO cambia

Mineclonia va **sin modificar** (carpeta `mineclonia/`): misma jugabilidad, mismos bloques,
mobs y texturas del mundo. Los cambios están en el motor (soporte de doble pantalla, táctil
y mando) y en la interfaz de los menús. La lista completa está en [CHANGELOG.md](CHANGELOG.md).

## Instalar

1. Descarga el zip de [Releases](../../releases) y descomprímelo.
2. Copia `Luanti.sh` y la carpeta `luanti` a `Roms/PORTS` de la SD.
3. En la consola: Ports → Luanti. La primera carga tarda un par de minutos.

Los controles y la solución de problemas están en [port/LEEME.txt](port/LEEME.txt).
Para compilarlo tú mismo, mira [build/README.md](build/README.md).

## Sobre el uso de IA

Lo digo de frente: yo no habría podido programar esto solo. Los cambios del motor los escribí
con ayuda de una IA (Claude), y yo lo fui probando en mi consola. La IA se usó únicamente para
la compatibilidad con las dos pantallas y para que la interfaz sea cómoda en esta consola. No
se tocó la jugabilidad ni el arte del juego. La idea es simple: que quienes tenemos una
RG DS Plus podamos disfrutar Mineclonia en ella.

## Gracias

A la gente de **Mineclonia** y de **VoxeLibre/MineClone2** antes que ellos, por años de trabajo
que yo solo tuve que adaptar. Al equipo de **Luanti**, por un motor libre que se deja modificar.
A **NilsSlui** y **youngelf**, que documentaron cómo funciona el firmware de la RG DS Plus: sin
sus notas el táctil seguiría sin responder. Y a **IdreesInc** por la fuente Monocraft.

Todo el mérito del juego es de ellos. — Nick

## Licencias

- Motor (`engine/`): Luanti, LGPL 2.1 o posterior — ver `engine/LICENSE.txt`.
- Juego (`mineclonia/`): código GPLv3 o posterior, arte CC BY-SA 4.0 — ver
  `mineclonia/LICENSE.txt`, `mineclonia/LEGAL.md` y `mineclonia/CREDITS.md`.
- Mis cambios se publican bajo esas mismas licencias. Detalle en [CREDITS.md](CREDITS.md).
