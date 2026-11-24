# Animación esquelética de Penta

Este documento explica cómo se animan los dos clips de Penta (entrada por la rampa y caída en el ring)
mediante huesos, pesos y *Linear Blend Skinning*, y en qué parte del código está cada paso.

## 1. Esqueleto

Un **esqueleto** es una jerarquía (árbol) de huesos. Cada hueso tiene una transformación **local**
respecto a su padre: traslación $T$, rotación $R$ (cuaternión) y escala $S$:

$$L = T \cdot R \cdot S$$

La transformación **global** de un hueso (en el espacio del modelo) se obtiene multiplicando desde la
raíz:

$$G_{\text{hueso}} = G_{\text{padre}} \cdot L_{\text{hueso}}, \qquad G_{\text{raíz}} = L_{\text{raíz}}$$

Penta usa el esqueleto estándar de Mixamo: 41 huesos (`mixamorig:Hips` es la raíz; de ahí salen la
columna, los brazos, las piernas y los dedos). En el FBX la jerarquía completa es
`RootNode → Armature → mixamorig:Hips → …` (52 nodos en total, incluidos los extremos `_end`).
De esos huesos, 34 tienen vértices asignados y son los que se mandan a la GPU.

## 2. Pesos de influencia

Cada vértice de la malla está "pegado" a uno o varios huesos con un **peso** $w_i \in [0,1]$ que indica
cuánto lo arrastra cada hueso, con $\sum_i w_i = 1$. Un vértice del codo, por ejemplo, se reparte entre
el brazo y el antebrazo, y así la piel se dobla de forma suave.

En el proyecto cada vértice guarda hasta **4** huesos (`Vertex::BoneIDs`, `ivec4`) y sus pesos
(`Vertex::Weights`, `vec4`) — `Mesh.h`. Si un vértice trae más de 4 influencias, Assimp conserva las 4 de
mayor peso (`aiProcess_LimitBoneWeights`) y el código las renormaliza para que sumen 1
(`Model::fillBoneWeights`). En Penta el 2.1 % de los vértices tenía 5 o 6 influencias; la diferencia
medida al descartar las menores es de 0.6 mm en promedio (16 mm en el peor vértice), imperceptible.

En OpenGL los identificadores son enteros y se enlazan con `glVertexAttribIPointer` (atributo 4) para
que no se conviertan a `float`; los pesos usan `glVertexAttribPointer` (atributo 5) —
`Mesh::setupMesh`.

## 3. Importación con Assimp

`Model::loadModel` lee el archivo con `aiProcess_Triangulate | aiProcess_LimitBoneWeights | …` y **sin**
`aiProcess_PreTransformVertices` (que aplanaría la jerarquía y destruiría el esqueleto). Antes de que
el `Assimp::Importer` libere la escena se copia todo lo necesario a estructuras propias
(`SkeletonData` en `Animation.h`):

| Assimp | Proyecto | Dónde |
|---|---|---|
| `aiNode` (árbol, `mTransformation`) | `SkelNode` (padre, matriz local de reposo y su descomposición T/R/S) | `Model::addSkelNode` |
| `aiBone` (`mName`, `mOffsetMatrix`) | `BoneSlot` (nodo + matriz offset) | `Model::findOrAddBone` |
| `aiVertexWeight` (`mVertexId`, `mWeight`) | `Vertex::BoneIDs/Weights` | `Model::fillBoneWeights` |
| `aiAnimation` (`mDuration`, `mTicksPerSecond`) | `AnimationClip` | `Model::readClips` |
| `aiNodeAnim` (claves de posición, rotación y escala) | `NodeChannel` | `Model::readClips` |

El enlace entre todo se hace **por nombre**: el nombre del hueso es el nombre de su nodo y el nombre del
canal de animación. Assimp guarda las matrices por filas y GLM por columnas, por eso se transponen:
`glm::transpose(glm::make_mat4(&m.a1))` (`Model::toGlm`).

## 4. Matriz *offset* (inverse bind matrix)

La malla se modeló en una pose fija, la **pose de enlace** (*bind pose*). La matriz offset de un hueso es
la inversa de su transformación global en esa pose:

$$O = G^{\text{bind}}_{\text{hueso}}{}^{-1}$$

Lleva un vértice del espacio del modelo al espacio local del hueso. Si después se aplica la global
actual del hueso, el vértice "viaja" con él:

$$M_{\text{hueso}}(t) = G_{\text{hueso}}(t)\cdot O$$

En la pose de enlace $M = I$ y la malla queda tal cual se modeló. Es la **matriz final** del hueso que
recibe el shader (`SkeletonData::Compose`).

En FBX la offset de Assimp parte del espacio del **modelo**, así que el vértice debe incluir la
transformación de su nodo de malla. En el FBX de Penta ese nodo tiene escala 2000 (escala del objeto en
Blender × conversión a centímetros); sin aplicarla la malla salía deformada. `Model::processMesh` la
aplica a las mallas con huesos de FBX (en glTF, por especificación, no se aplica). Si algún vértice no
trae pesos, sigue a su nodo de malla con un hueso sintético cuyo offset es la inversa de esa
transformación, para no aplicarla dos veces (`Model::fillBoneWeights`).

## 5. Interpolación de *keyframes*

Un clip guarda claves $(t_k, \text{valor}_k)$ por hueso. El tiempo real se convierte a *ticks* del archivo:
$\text{tick} = t_{\text{seg}}\cdot \text{ticksPerSecond}$ (24 en ambos clips). Para un tick entre dos claves
$t_k \le \text{tick} < t_{k+1}$:

$$f = \frac{\text{tick}-t_k}{t_{k+1}-t_k}$$

- **Posición y escala** — interpolación lineal: $p = (1-f)\,p_k + f\,p_{k+1}$
- **Rotación** — SLERP de cuaterniones por el camino corto:
  $$\operatorname{slerp}(q_k,q_{k+1},f)=\frac{\sin((1-f)\theta)}{\sin\theta}q_k+\frac{\sin(f\theta)}{\sin\theta}q_{k+1},\quad \cos\theta = q_k\cdot q_{k+1}$$

Casos especiales (`AnimMath::Sample`, `NodeChannel::Sample`): sin claves se usa el valor de reposo del
nodo; con una clave se usa esa; antes de la primera o después de la última se usa el extremo; duración
0 o ticks/s indefinidos (se usa 25). Los nodos que el clip no anima conservan su matriz de reposo.

La animación avanza con el **tiempo real** (`Animator::Update(deltaTime)`), no por cuadros: con 47 fps
(Debug) y 140 fps (Release) la entrada dura 5.17 s y la caída 3.71 s en ambos casos.

**Transición suave.** Cuando la pose nueva empieza cerca de la actual (la cadera a menos de 0.3 m, por
ejemplo al pasar de la pose de espera a la caída) se mezclan ambas durante 0.4 s, hueso por hueso:
posición y escala lineal, rotación con SLERP, con un factor *smoothstep* $f^2(3-2f)$
(`NodePose::Blend`, `Animator::evaluate`). Si están lejos (Penta tumbado, o de la rampa al ring) el
cambio es directo: mezclar la traslación de la cadera haría que el cuerpo se deslizara por la lona
(`PentaAnimado::fundidoEnRing`).

## 6. Transformaciones jerárquicas

`SkeletonData::Compose` recorre los nodos en orden (cada padre está antes que sus hijos) y calcula
$G = G_{\text{padre}}\cdot L$ en un solo ciclo, sin recursión. Después:

$$M_b = G_{\text{nodo}(b)} \cdot O_b \qquad b = 0,\dots,33$$

## 7. Linear Blend Skinning (LBS)

Cada vértice se transforma con la combinación ponderada de las matrices de sus huesos
(`Shaders/lighting.vs`):

$$v' = \Big(\sum_{i=1}^{4} w_i\, M_{\text{id}_i}\Big)\, v, \qquad n' = \operatorname{normalize}\Big(\big(\textstyle\sum_i w_i M_{\text{id}_i}\big)_{3\times3}\, n\Big)$$

Para la normal basta la parte 3×3: los huesos de Penta solo rotan y escalan de forma uniforme (se
comprobó: diferencia entre escalas de ejes < $1.3\cdot10^{-5}$), y `normalize` elimina la escala. Después
se aplica la matriz del objeto como a cualquier modelo: $\text{gl\_Position} = P\cdot V\cdot \text{Model}\cdot v'$.
Si un vértice no tuviera pesos, la matriz es la identidad y no colapsa al origen.

Las 34 matrices viajan en un **Uniform Buffer Object** (`layout(std140) uniform Bones`), que llena
`BoneBuffer::Upload` (`Scene.h`). Un arreglo de *uniforms* normal de 100 `mat4` ocupa 1600 componentes y
el mínimo garantizado por OpenGL 3.3 es 1024; un bloque uniforme tiene garantizados 16 KB (256 `mat4`).

## 8. Qué se calcula en CPU y qué en GPU

| CPU (una vez por cuadro, ~52 nodos) | GPU (por vértice, ~42 600 vértices) |
|---|---|
| Tiempo del clip y transición (`Animator::Update`) | Mezcla de las 4 matrices de cada vértice |
| Interpolación de claves (`SkeletonData::SamplePose`) | Posición y normal deformadas |
| Globales y matrices finales (`SkeletonData::Compose`) | Proyección e iluminación (Phong + Fresnel) |
| Envío de matrices al UBO (`BoneBuffer::Upload`) | |

## 9. Implementación en este proyecto

**Archivos de animación.** `Animacion_EntradaPenta.fbx` (exportado desde Blender) y
`Animación_CaídaPenta.fbx` (descargado de Mixamo) tienen esqueletos con **ejes locales distintos**: el de
Mixamo usa nodos `$AssimpFbx$_PreRotation` y no tiene nodo `Armature`, así que sus claves no sirven para
el esqueleto de la entrada. Como el `.blend` de la caída contiene el mismo armature con la misma acción
(se verificó que la acción coincide exactamente con el FBX de Mixamo), se **reexportó** con la misma
configuración que la entrada (`herramientas/exportar_animacion_penta.py`). Resultado: un solo modelo con
dos clips, sin *retargeting* y sin modificar los movimientos:

```
modelos/modelos_importados/penta_animado/
  Penta_Entrada.fbx   copia exacta de Animacion_EntradaPenta.fbx: malla, texturas, esqueleto y clip "Armature|mixamo.com"
  Penta_Caida.fbx     solo esqueleto + clip "Armature|Caida" (mismas matrices offset y mismos nodos)
```

La única diferencia aplicada al exportar fue igualar la escala del objeto `Armature` (30 en el `.blend`
de la caída, 20 en el de la entrada); es una escala uniforme que no altera el movimiento.

**Root motion.** Ambos clips traen el desplazamiento en el hueso `Hips` (la entrada avanza 3.5 m con un
salto de ~1 m; la caída lleva al personaje 2 m hacia su derecha). Por eso el objeto se coloca en el punto
de partida (`PENTA_RAMPA`, `PENTA_RING` en `ArenaLayout.h`) y no se le suma movimiento por código. La única
corrección es vertical: la pasarela baja hacia el ring y el clip se animó sobre piso plano, así que
durante la entrada la altura del objeto sigue la pasarela bajo la cadera (`PentaAnimado::seguirPasarela`,
`AlturaPasarela`).

**Estados** (`PentaAnimado.h`): Reposo → Entrada → Entrada terminada; Caída → Caída terminada; F7 vuelve
a Reposo. Al terminar un clip se conserva su último cuadro. La consola registra cada cambio de estado,
el tiempo real que duró el anterior y si fue con transición suave o directo.

**Contacto con la lona.** Durante la caída la malla baja hasta 8 cm bajo el piso ya tumbado (16 cm en el
impacto). Es igual en Blender (la animación original de Mixamo lo hace), así que se conservó tal cual.

**Esqueleto en pantalla** (F8, `DebugDraw::DrawSkeleton`): la posición de cada articulación es la columna
de traslación de su global, $\text{Model}\cdot G\cdot(0,0,0,1)^T$; se dibuja una línea a su padre (izquierda
verde, derecha roja, centro amarillo) sin prueba de profundidad.

**Verificación.** Las posiciones de las articulaciones que calcula el motor se compararon cuadro por
cuadro con las de Blender: diferencia máxima de 0.016 mm en la caída y 7 mm en la entrada (su FBX se
exportó con reducción de claves).

## 10. Para estudiar el código (en orden)

1. `Mesh.h` — `Vertex` (`BoneIDs`, `Weights`) y `setupMesh` (atributos 4 y 5).
2. `Model.h` — `loadModel`, `prepareSkeleton`, `readClips`, `fillBoneWeights`, `processMesh`.
3. `Animation.h` — `AnimMath::Sample`, `NodeChannel::Sample`, `SkeletonData::SamplePose/Compose`, `Animator`.
4. `Shaders/lighting.vs` — bloque `Bones` y la suma ponderada.
5. `Scene.h` — `BoneBuffer` y `drawPass`.
6. `PentaAnimado.h` y las teclas F5–F8 en `luchaLibreCompuG.cpp`.
