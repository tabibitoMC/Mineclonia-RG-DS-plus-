# Cambios respecto a los proyectos originales

Base: **Luanti 5.18.0** y **Mineclonia 0.124.1**.

## Mineclonia (`mineclonia/`)
Sin cambios. Es la versión 0.124.1 tal cual.

## Motor Luanti (`engine/`)
El diff completo está en `patches/rgds-plus-luanti-5.18.0.patch`.

- **Doble pantalla:** segunda ventana en la pantalla inferior que comparte el contexto
  gráfico. Los menús, el inventario y el chat se dibujan ahí.
- **Pantalla inferior en el juego:** barra de objetos táctil, coordenadas, zona tipo
  touchpad para la cámara y botones de inventario, cámara y pausa. La barra de objetos
  ya no se dibuja arriba.
- **Táctil:** lectura directa del panel (evdev) y activación del panel, que el menú de la
  consola apaga al abrir programas externos.
- **Mando:** navegación de menús con cruceta/stick, A y B; L1/R1 cambian de pestaña.
- **Menú principal:** pantalla de título arriba y menú abajo; tema de botones y fondo.
- **Memoria:** tope al tamaño de las fuentes (una fuente de 190 pt agotaba la RAM) y la
  pantalla inferior solo se redibuja cuando cambia.
- **Texto:** conversión UTF-8 propia, porque el firmware no trae los módulos gconv.

## Archivos del port (`port/`)
- `Luanti.sh`: lanzador (mapeo del mando, táctil, memoria swap, registros).
- `minetest.conf`: controles y gráficos ajustados a la consola.
- `assets/`: botones, iconos y fondos de la interfaz; fuente Monocraft.
