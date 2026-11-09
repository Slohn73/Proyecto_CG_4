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

## Ejecutar sin Visual Studio

El repositorio incluye el ejecutable **Release | Win32** ya compilado; no hace falta compilar nada.

- **Dónde está:** `luchaLibreCompuG/luchaLibreCompuG/luchaLibreCompuG.exe`. Esa carpeta ya contiene
  `Shaders/`, `Skybox/`, `modelos/` y las DLL (`glew32.dll`, `assimp-vc140-mt.dll`) que el EXE necesita.
  Hay que conservarlas juntas: se puede copiar o mover la carpeta completa, pero no el EXE solo.
- **Cómo abrirlo:** clonar o descargar la rama `entrega/laboratorio` y hacer doble clic en el EXE.
  El programa toma como carpeta de recursos la del propio EXE, así que no importa desde dónde se abra
  (Explorador, acceso directo o consola). Se abre una ventana de consola (informa de la carga) y la
  ventana de la arena; la carga tarda unos 20 s. Si no encuentra los recursos o la tarjeta gráfica no
  soporta OpenGL 3.3, muestra un cuadro con el motivo.
- **Requisitos:** Windows 10 u 11 (el EXE es de 32 bits y funciona en Windows de 32 y 64 bits), una GPU
  con controlador OpenGL 3.3 o superior y el **Microsoft Visual C++ Redistributable 2015-2022 (x86)**
  (aporta `MSVCP140.dll` y `VCRUNTIME140.dll`; [descarga oficial](https://aka.ms/vs/17/release/vc_redist.x86.exe)).
  No se distribuye con el repositorio; la mayoría de los equipos con juegos o programas recientes ya lo
  tiene. No se requiere Visual Studio ni la carpeta `Debug`.
- **Teclas principales:** W A S D / flechas y mouse para moverse, Shift para ir más rápido,
  **1 – 8** para las vistas de cámara, **L** cambia el color de las luces y las cinco animaciones:
  **T** árbitro, **Espacio** combate, **B** burrito, **X** espectador bebiendo y **Z** espectador
  lanzando la cerveza. La tabla completa está en *Controles*.
- **Regresar a la vista inicial:** tecla **R**.
- **Cerrar:** tecla **Esc** o el botón de cerrar de la ventana (también se cierra al cerrar la consola).
  F12 guarda una captura `captura_N.png` en la carpeta del EXE.

### Volver a generar el ejecutable Release

Con Visual Studio (carga de trabajo de C++, toolset v145) instalado, desde la raíz del repositorio:

```
powershell -ExecutionPolicy Bypass -File luchaLibreCompuG/luchaLibreCompuG/herramientas/construir_distribucion.ps1
```

El script compila Release | Win32 con MSBuild, copia `Release\luchaLibreCompuG.exe` a
`luchaLibreCompuG/luchaLibreCompuG/luchaLibreCompuG.exe` y verifica con `dumpbin` que el EXE y las DLL
no dependen de los runtimes de depuración (`MSVCP140D`, `VCRUNTIME140D`, `ucrtbased`) y que no falta
ninguna DLL. Con `-SinCompilar` solo copia y verifica. Después, para volver a probar la distribución
en una carpeta aparte (sin acceso a las carpetas de desarrollo):

```
powershell -ExecutionPolicy Bypass -File luchaLibreCompuG/luchaLibreCompuG/herramientas/probar_distribucion.ps1
```

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
| 7 | Vista del espectador E de frente: para ver X (beber) y Z (lanzar la cerveza) |
| 8 | Ring de cerca: luchadores y árbitro de cuerpo completo, y la lona donde caen el burrito y la cerveza (T, Espacio, B, Z) |
| C | Liberar / capturar el cursor |
| B | Un espectador de las gradas norte lanza un burrito al ring (tiro parabólico con g = 9.81 m/s²); cae acostado, queda en la lona y se encoge. No se reinicia a mitad del vuelo |
| Z | El espectador E (primera fila norte) arma el brazo izquierdo sobre la cabeza y lanza su cerveza al ring; la botella cae acostada en la lona, se encoge y le aparece otra en la mano |
| X | El espectador E sube la botella derecha a la boca (hombro y codo), da un trago y la baja |
| L | Cambiar el color de las luces |
| T | El árbitro se acerca al centro, observa a los luchadores, da la señal de inicio con el brazo derecho y regresa a su lugar (se ve en la vista 2; se puede repetir al terminar) |
| Espacio | Combate: el luchador camina hacia Penta y le da un golpe al pecho; Penta reacciona y ambos regresan. No se reinicia si se repite la tecla; si el árbitro está en su secuencia, el combate espera a que termine (y T se ignora durante el combate) |
| F1 | Cuadrícula de 1 m y ejes (X rojo, Y verde, Z azul) |
| F2 | Cajas envolventes de todos los objetos |
| Tab / Shift+Tab | Seleccionar objeto (caja amarilla; su posición aparece en el título de la ventana) |
| H | Ocultar / mostrar el objeto seleccionado |
| F3 | Imprimir en consola el reporte de modelos y posiciones |
| F4 | Volver a mostrar todos los objetos |
| F12 | Guardar captura `captura_N.png` |

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
luchaLibreCompuG.exe --vista 2 --prueba-arbitro arbitro   # corre 2 veces la animación T, guarda capturas y mide holguras
luchaLibreCompuG.exe --limitar-fps 20          # limita los FPS (para comprobar que las animaciones no dependen de ellos)
luchaLibreCompuG.exe --camara 0.3 2.25 2.8 -90 -3 --prueba-combate combate   # corre 2 veces el combate, capturas y mediciones
luchaLibreCompuG.exe --camara 0.3 2.25 2.8 -90 -3 --video-combate carpeta    # cuadros del combate a 30 por segundo
luchaLibreCompuG.exe --congelar-combate 3.05    # muestra el combate detenido en ese instante
luchaLibreCompuG.exe --camara 1.6 1.7 5.6 125 -15 --video-teclas Z@0,X@3 6 carpeta   # pulsa teclas en esos instantes y guarda los cuadros (paso fijo de 1/30 s)
```

## Modelos convertidos

El cinturón, los espectadores (solo existían como `.blend`) y las cámaras low-poly (`.glb`,
formato que el Assimp incluido no reconoce) se exportaron a OBJ con
`luchaLibreCompuG/luchaLibreCompuG/herramientas/convertir_blend.py`
(`blender -b --factory-startup --python herramientas/convertir_blend.py`). Los archivos
originales no se modificaron.

El árbitro no tiene esqueleto; para la señal de la tecla T se separó su brazo derecho (con la
manga) en `arbitro-ruper-design/separado/` con `herramientas/separar_arbitro.py`, sin cambiar la
geometría. La animación y sus parámetros están en `AnimacionArbitro.h`.

Los dos luchadores (`Luchador_001.fbx` y `Taunt.fbx`) traen esqueleto Mixamo. Se cargan con
`ModelOptions::skinning`: el motor lee huesos y pesos, `AnimacionCombate.h` calcula el giro de cada
articulación por código y `Shaders/lighting.vs` deforma la malla. Las animaciones que traen esos FBX
no se usan y los archivos no se modificaron.
