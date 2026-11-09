# Proyecto Final CG — Lucha Libre Mexicana ("Little Endians", Grupo 04)

Escena 3D de una arena de lucha libre en C++ con OpenGL 3.3, GLFW, GLEW, GLM, Assimp y SOIL2.

## Cómo ejecutar

1. Abrir `luchaLibreCompuG/luchaLibreCompuG.slnx` con Visual Studio (probado con VS 2026, toolset v145).
2. Elegir la configuración **Debug | x86** (o Release | x86). Todas las bibliotecas incluidas
   (GLEW, GLFW, Assimp, SOIL2) son de 32 bits, por eso no existe configuración x64.
3. Ejecutar con F5. El directorio de trabajo debe ser `luchaLibreCompuG/luchaLibreCompuG/`
   (es el predeterminado de Visual Studio): ahí están los shaders, modelos, skybox y DLLs.

La carga de modelos tarda unos 20 s (más si la laptop está en batería). Release es notablemente
más rápido que Debug al cargar.

## Controles

| Tecla | Acción |
|---|---|
| W A S D / flechas | Mover la cámara |
| Mouse | Mirar |
| Q / E | Bajar / subir |
| Shift | Movimiento rápido (x4) |
| Rueda del mouse | Zoom |
| R | Regresar a la vista inicial |
| 1 – 6 | Vistas: general, ring, entrada, desde el público, cenital, mesa de comentaristas |
| C | Liberar / capturar el cursor |
| B | Lanzar el burrito desde el público al ring |
| Z | El espectador E lanza su cerveza con el antebrazo izquierdo; se repone al terminar |
| X | El espectador E levanta el brazo derecho para beber y lo baja |
| L | Cambiar el color de las luces |
| T | Animación del árbitro (antes era A, que choca con mover la cámara) |
| Espacio | Ataque del luchador |
| F1 | Cuadrícula de 1 m y ejes (X rojo, Y verde, Z azul) |
| F2 | Cajas envolventes de todos los objetos |
| Tab / Shift+Tab | Seleccionar objeto (caja amarilla; su posición aparece en el título de la ventana) |
| H | Ocultar / mostrar el objeto seleccionado |
| F3 | Imprimir en consola el reporte de modelos y posiciones |
| F4 | Volver a mostrar todos los objetos |
| F12 | Guardar captura `captura_N.png` |

## Animaciones del espectador E

El espectador E está sentado en `(0, 0, 6.9)`, detrás de la cámara con tripié y junto
al espectador de camisa azul. Tiene una botella de cerveza en cada mano. Para verlo,
seleccionar la vista **2**, girar hacia el público con el mouse y ajustar la posición
con **W A S D**.

- **Z — Lanzar:** el antebrazo izquierdo gira alrededor del codo sobre los ejes X e Y,
  mientras la mano y la botella acompañan el movimiento. El brazo superior permanece
  quieto. La botella se suelta a los **0.70 s**, gira en el aire y sigue una parábola
  hacia el mismo destino del burrito en el ring. El vuelo dura **1.25 s**; al terminar
  se oculta y se repone en la mano para poder repetir el lanzamiento.
- **X — Beber:** el brazo derecho gira alrededor del hombro; el antebrazo, la mano y
  la botella lo siguen. Sube durante **0.9 s**, mantiene la botella cerca de la boca
  e incluye una inclinación de sorbo durante **1 s**, y baja durante **0.9 s**.

Las dos acciones pueden ejecutarse a la vez. Pulsar de nuevo la misma tecla durante
su animación no la reinicia. Los movimientos se actualizan con `deltaTime` y regresan
a la postura de reposo al terminar. El lanzamiento no incluye colisiones ni rebotes.

### Archivos y parámetros

- `luchaLibreCompuG/luchaLibreCompuG/ArenaLayout.h`: registra las piezas y la cerveza,
  y coloca al personaje mediante `posicionE` en `BuildArena`.
- `luchaLibreCompuG/luchaLibreCompuG/luchaLibreCompuG.cpp`: contiene los pivotes
  `CODO_IZQ` y `HOMBRO_DER`, los puntos de agarre, la lógica de `Animation()` y las
  teclas de `KeyCallback`. `SUELTA_CERVEZA` controla el instante de suelta y
  `ALTURA_CERVEZA` la elevación adicional de la trayectoria, actualmente **2.5 m**.
- `modelos/modelos_propios/espectador_E_separado/`: contiene siete piezas OBJ
  (cuerpo, dos brazos, dos antebrazos y dos manos) y sus materiales, conservando
  el origen común para ensamblarlas.
- `modelos/modelos_propios/botella_libre/`: contiene el OBJ de la botella, sus
  materiales y la textura `etiqueta_libre.png`. Las rutas de modelos son relativas
  al directorio de ejecución `luchaLibreCompuG/luchaLibreCompuG/`.

## Dónde se modifica la escena

Todo está en **`luchaLibreCompuG/luchaLibreCompuG/ArenaLayout.h`**:

- `namespace Arena`: medidas generales (distancia de barreras, filas de sillas, gradas, etc.).
- `RegisterArenaAssets`: qué archivos se cargan y cómo se normalizan (`unitScale` o
  `targetHeight` para llevar a metros, `baseRotation` para que el frente mire a +Z, `pivot`),
  partes que se excluyen y texturas asignadas a materiales que el archivo no enlaza.
- `BuildArena`: posición (`At`), giro (`RotY`/`Rot`), escala (`Scale`) y visibilidad (`Hidden`)
  de cada objeto. Un mismo modelo se reutiliza en varias instancias.
- `BuildLightRig`: luces OpenGL (direccional, 4 puntuales, reflectores).
- `GetCameraViews`: vistas de las teclas 1–6.

Convenciones: 1 unidad = 1 metro, Y hacia arriba, origen en el centro del ring a nivel del piso;
la rampa está del lado −Z.

Para animar un objeto basta con cambiar su `position`, `rotation` o `scale` en `Animation()`
(`luchaLibreCompuG.cpp`); el dibujo no cambia. Ejemplos: cinturón giratorio, bocinas y burrito.

## Opciones de línea de comandos (revisión sin interacción)

```
luchaLibreCompuG.exe --galeria                 # una instancia de cada modelo en fila
luchaLibreCompuG.exe --vista 3                 # vista inicial
luchaLibreCompuG.exe --camara x y z yaw pitch  # posición de cámara exacta
luchaLibreCompuG.exe --depuracion              # cuadrícula, ejes y cajas
luchaLibreCompuG.exe --captura foto.png        # guarda una imagen y termina
luchaLibreCompuG.exe --captura-todas prefijo   # una imagen por vista
luchaLibreCompuG.exe --segundos 10             # termina tras N s e informa los FPS
```

## Modelos convertidos

El cinturón, los espectadores (solo existían como `.blend`) y las cámaras low-poly (`.glb`,
formato que el Assimp incluido no reconoce) se exportaron a OBJ con
`luchaLibreCompuG/luchaLibreCompuG/herramientas/convertir_blend.py`
(`blender -b --factory-startup --python herramientas/convertir_blend.py`). Los archivos
originales no se modificaron.
