# Proyecto Final CG — Lucha Libre Mexicana ("Little Endians", Grupo 04)

Escena 3D de una arena de lucha libre en C++ con OpenGL 3.3, GLFW, GLEW, GLM, Assimp 5.4.1 y SOIL2.

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
| F5 | Penta: animación de entrada por la rampa |
| F6 | Penta: animación de caída en el ring |
| F7 | Penta: volver a la pose de espera en el ring |
| F8 | Mostrar / ocultar el esqueleto de Penta (huesos y articulaciones) |
| F1 | Cuadrícula de 1 m y ejes (X rojo, Y verde, Z azul) |
| F2 | Cajas envolventes de todos los objetos |
| Tab / Shift+Tab | Seleccionar objeto (caja amarilla; su posición aparece en el título de la ventana) |
| H | Ocultar / mostrar el objeto seleccionado |
| F3 | Imprimir en consola el reporte de modelos y posiciones |
| F4 | Volver a mostrar todos los objetos |
| F12 | Guardar captura `captura_N.png` |

F5 también inicia la depuración en Visual Studio, pero solo cuando la ventana enfocada es la de Visual
Studio; con la ventana de la arena enfocada, F5 llega al programa. Las teclas de Penta actúan una vez por
pulsación (mantenerlas presionadas no reinicia el clip).

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
luchaLibreCompuG.exe --penta entrada           # inicia con la entrada de Penta (o "caida")
luchaLibreCompuG.exe --penta caida 1.9         # pose congelada de la caída en t = 1.9 s
luchaLibreCompuG.exe --esqueleto               # esqueleto de Penta visible
luchaLibreCompuG.exe --captura-tras 4 foto.png # toma la captura a los 4 s (con --captura)
```

Ejemplo (pose intermedia de la caída con esqueleto):
`luchaLibreCompuG.exe --penta caida 1.9 --esqueleto --camara 1.5 4.0 4.0 -89 -28 --captura caida.png`

## Animación esquelética de Penta

Penta reproduce dos animaciones originales hechas en Blender (entrada y caída) mediante huesos, pesos
por vértice, matrices offset, interpolación de *keyframes* (SLERP en rotaciones) y *Linear Blend
Skinning* en el vertex shader. La explicación completa, con ecuaciones y referencias al código, está en
[`ANIMACION_ESQUELETICA.md`](ANIMACION_ESQUELETICA.md).

- Recursos: `modelos/modelos_importados/penta_animado/` (`Penta_Entrada.fbx`, copia exacta del FBX de la
  entrada, y `Penta_Caida.fbx`, el clip de la caída). Código: `Animation.h`, `Model.h`, `PentaAnimado.h`,
  `Shaders/lighting.vs`.
- Originales editables: carpeta `Animaciones_Penta/` en la raíz (no se usa al ejecutar; no hace falta
  Blender para correr el proyecto).
- `Penta_Caida.fbx` se generó desde `Animacion_CaidaPenta.blend` para que comparta exactamente el
  esqueleto de la entrada (el FBX de Mixamo de la caída tiene otros ejes locales). Para regenerarlo,
  desde `luchaLibreCompuG/luchaLibreCompuG` (el `.blend` no se modifica):
  ```
  blender -b --factory-startup "../../Animaciones_Penta/Animacion_CaidaPenta.blend" --python herramientas/exportar_animacion_penta.py -- modelos/modelos_importados/penta_animado/Penta_Caida.fbx --nombre Caida
  ```
- Las matrices de los huesos se envían en un *Uniform Buffer Object* (bloque `Bones`), compatible con
  cualquier GPU con OpenGL 3.3. Al iniciar, la consola muestra los límites de la GPU y al terminar el
  número de errores de OpenGL.


## Modelos convertidos

El cinturón, los espectadores (solo existían como `.blend`) y las cámaras low-poly (`.glb`,
formato que el Assimp incluido no reconoce) se exportaron a OBJ con
`luchaLibreCompuG/luchaLibreCompuG/herramientas/convertir_blend.py`
(`blender -b --factory-startup --python herramientas/convertir_blend.py`). Los archivos
originales no se modificaron.
