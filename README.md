# Proyecto Final CG — Lucha Libre Mexicana ("Little Endians", Grupo 04)

Escena 3D de una arena de lucha libre en C++ con OpenGL 3.3, GLFW, GLEW, GLM, Assimp 5.4.1 y SOIL2.

## Requisitos

- Windows 10 o superior (el programa es de 32 bits y corre también en Windows de 64 bits).
- GPU y controlador con **OpenGL 3.3 o superior**.
- 4 GB de RAM, monitor de 1280 x 720 o mayor, teclado y mouse.
- Microsoft Visual C++ Redistributable 2015-2022 (x86).
- Para compilar: Visual Studio (probado con VS 2026, toolset v145) con la carga de trabajo de C++ para escritorio.

## Cómo ejecutar

### Opción A: instalador

Ejecutar `luchaLibreCompuG_Setup.exe` (generado con InstallForge), seguir el asistente y abrir
**Lucha Libre CG** desde el escritorio o el menú Inicio. Las instrucciones completas están en
[`Manual de instalación y uso de software.txt`](Manual%20de%20instalaci%C3%B3n%20y%20uso%20de%20software.txt).

### Opción B: desde el repositorio

1. Clonar el repositorio. No hay que instalar bibliotecas: GLEW, GLFW, Assimp, GLM y SOIL2 ya vienen incluidas.
2. Abrir `luchaLibreCompuG/luchaLibreCompuG.slnx` con Visual Studio.
3. Elegir la configuración **Debug | x86** (o Release | x86). Todas las bibliotecas incluidas
   (GLEW, GLFW, Assimp, SOIL2) son de 32 bits, por eso no existe configuración x64.
4. Ejecutar con F5. El directorio de trabajo debe ser `luchaLibreCompuG/luchaLibreCompuG/`
   (es el predeterminado de Visual Studio): ahí están los shaders, modelos, skybox y DLLs.

La carga de modelos tarda unos 20 s (más si la laptop está en batería). Release es notablemente
más rápido que Debug al cargar.

## Controles

| Tecla | Acción |
|---|---|
| W A S D / flechas | Mover la cámara |
| Mouse | Mirar |
| E / Q | Subir / bajar |
| Shift | Movimiento rápido (x4) |
| Rueda del mouse | Zoom |
| R | Regresar a la vista inicial |
| 1 – 6 | Vistas: general, ring, entrada, desde el público, cenital, mesa de comentaristas |
| C | Liberar / capturar el cursor |
| Esc | Salir |
| B | Lanzar el burrito desde el público al ring |
| Z | El espectador E lanza su cerveza con el antebrazo izquierdo; se repone al terminar |
| X | El espectador E levanta el brazo derecho para beber y lo baja |
| L | Cambiar el color de las luces |
| G | Activar / pausar la bandera animada |
| T | Animación del árbitro (antes era A, que choca con mover la cámara) |
| Espacio | Ataque del luchador |
| M | El minion alza los brazos y lanza la silla |
| K | Pausar / reanudar los reflectores del marcador |
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
Studio; con la ventana de la arena enfocada, F5 llega al programa. Las teclas de animación actúan una vez
por pulsación (mantenerlas presionadas no reinicia el clip).

## Técnicas gráficas

### Iluminación (dos modelos)

Implementada en `Shaders/lighting.frag`:

- **Phong**: componentes ambiental, difusa y especular para la luz direccional, las 4 luces puntuales y los
  reflectores (parámetros en `Lights.h`; se configuran en `BuildLightRig`).
- **Fresnel (aproximación de Schlick)**: en materiales con `reflectivity > 0` se mezcla el color de Phong
  con el reflejo del entorno según el ángulo de visión: `F = F0 + (1 - F0) * (1 - cosθ)^5`. Los bordes y
  ángulos rasantes reflejan más. Se aplica a las barreras, el cinturón y las sillas.
  La reflectividad de cada modelo se ajusta en `ArenaLayout.h` con `options.reflectivityOverrides`
  (por nombre de material, o `"*"` para todos; 0 = sin reflejo, 1 = máximo).

### Texturizado de entorno (cube mapping)

El skybox usa un cubemap de 6 caras (`Skybox/`), dibujado con su propio shader (`skybox.vs/.frag`), por lo
que es independiente de los modelos de iluminación. Ese mismo cubemap se envía al shader de iluminación
(unidad de textura 2, uniform `envMap`) para el reflejo de Fresnel.

### Animación (jerárquica)

- **Básica (por código)**: rotación continua del cinturón, vibración de las bocinas, cerveza de la mesa que brinca.
- **Por keyframes (esquelética / nodos)**: bandera, luchador, minion, reflectores y Penta, con huesos, pesos
  por vértice e interpolación de claves (ver `Animation.h`).
- **Procedural jerárquica**: el espectador E (brazo → antebrazo → mano → cerveza, con articulaciones en
  codo y hombro) y la trayectoria parabólica del burrito y de la cerveza lanzada.

## Dónde se modifica la escena

Todo está en **`luchaLibreCompuG/luchaLibreCompuG/ArenaLayout.h`**:

- `namespace Arena`: medidas generales (distancia de barreras, filas de sillas, gradas, etc.).
- `RegisterArenaAssets`: qué archivos se cargan y cómo se normalizan (`unitScale` o
  `targetHeight` para llevar a metros, `baseRotation` para que el frente mire a +Z, `pivot`),
  partes que se excluyen, texturas asignadas a materiales que el archivo no enlaza y reflectividad
  (`reflectivityOverrides`).
- `BuildArena`: posición (`At`), giro (`RotY`/`Rot`), escala (`Scale`) y visibilidad (`Hidden`)
  de cada objeto. Un mismo modelo se reutiliza en varias instancias.
- `BuildLightRig`: luces OpenGL (direccional, 4 puntuales, reflectores).
- `GetCameraViews`: vistas de las teclas 1–6.

Convenciones: 1 unidad = 1 metro, Y hacia arriba, origen en el centro del ring a nivel del piso;
la rampa está del lado −Z.

Para animar un objeto basta con cambiar su `position`, `rotation` o `scale` en `Animation()`
(`luchaLibreCompuG.cpp`); el dibujo no cambia. Ejemplos: cinturón giratorio, bocinas y burrito.

Para recortar una animación importada (en segundos, no en cuadros) se usa
`modelo->SetAnimationRange(inicio, fin)`; por ejemplo, la bandera usa los cuadros 1–90 a 24 fps.

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

## Generar el instalador (InstallForge)

Compilar en **Release | x86**, reunir `luchaLibreCompuG.exe`, las DLL y las carpetas `Shaders`, `modelos`
y `Skybox` (con sus subcarpetas) y empaquetarlas con InstallForge. El acceso directo debe iniciar en la
carpeta de instalación, porque el programa busca ahí sus recursos. Pasos detallados en la sección 9 del
manual de instalación y uso.
