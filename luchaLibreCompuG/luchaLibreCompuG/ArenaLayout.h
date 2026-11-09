#pragma once

// =====================================================================================
//  CONFIGURACIÓN DE LA ARENA
//  Aquí se centralizan: los modelos que se cargan (y cómo se normalizan), la posición,
//  rotación y escala de cada objeto, las luces y las vistas de cámara.
//
//  Convenciones del mundo:
//   - 1 unidad = 1 metro, eje Y hacia arriba.
//   - Origen: centro del ring a nivel del piso de la arena (y = 0).
//   - La rampa de entrada está del lado -Z; el lado +Z queda frente a ella.
//   - En el espacio normalizado de cada modelo el frente mira hacia +Z, así que
//     RotY(0) mira hacia +Z, RotY(180) hacia -Z, RotY(90) hacia +X y RotY(-90) hacia -X.
// =====================================================================================

#include <string>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>

#include "Scene.h"
#include "Lights.h"

// -------------------------------------------------------------------------------------
//  Medidas generales de la distribución (metros, desde el centro del ring)
// -------------------------------------------------------------------------------------
namespace Arena
{
	const float RING_MITAD = 3.05f;          // Medio lado de la lona (ring de 6.10 m, medido del modelo)
	const float RAMPA_INICIO = 3.30f;        // La rampa toca el ring en z = -RAMPA_INICIO
	const float BARRERA_DIST = 6.00f;        // Línea de barreras de protección
	const float BARRERA_PASO = 1.695f;       // Separación entre barreras (largo de una barrera)
	const float SILLAS_FILA_1 = 6.90f;       // Filas de sillas de primera fila (ringside)
	const float SILLAS_FILA_2 = 7.90f;
	const float SILLAS_PASO = 0.60f;
	const float SILLAS_TANGENTE = 5.40f;     // Las filas de sillas abarcan [-5.4, 5.4] a lo largo de cada lado
	const float GRADAS_FRENTE = 9.00f;       // Primer peldaño de las gradas
	const float GRADAS_SECCION = 3.36f;      // Desplazamiento lateral de las 2 secciones de cada lado
	const float PASILLO_RAMPA = 2.60f;       // Medio ancho del pasillo de la rampa entre gradas
	const float MESA_X = -4.50f;             // Mesa de comentaristas (lado -X, dentro de las barreras)
	const float MARCADOR_ALTURA = 8.60f;     // Centro del marcador colgante
	const float LUMINARIAS_ALTURA = 7.00f;
	const float LUMINARIAS_DIST = 3.00f;
	const float OCUPACION_GRADAS = 0.70f;    // Fracción de asientos ocupados
	const float OCUPACION_SILLAS = 0.75f;
}

// Trayectoria del burrito volador (tecla B): desde el público hasta la lona del ring.
// Se recalculan en BuildArena con la altura real de las gradas y de la lona.
inline glm::vec3 BURRITO_INICIO(4.0f, 2.5f, 11.0f);
inline glm::vec3 BURRITO_FIN(0.0f, 1.3f, 0.0f);
const float BURRITO_ALTURA = 3.0f;

// -------------------------------------------------------------------------------------
//  1. Recursos: archivo, unidades y corrección de orientación/pivote
// -------------------------------------------------------------------------------------
inline std::unique_ptr<Model> CreateFloorModel(float size, const glm::vec3& color)
{
	float h = size * 0.5f;
	float uv = size / 4.0f;
	std::vector<Vertex> v = {
		{ glm::vec3(-h, 0.0f, -h), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, 0.0f) },
		{ glm::vec3(h, 0.0f, -h), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(uv, 0.0f) },
		{ glm::vec3(h, 0.0f, h), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(uv, uv) },
		{ glm::vec3(-h, 0.0f, h), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f, uv) } };
	std::vector<GLuint> idx = { 0, 2, 1, 0, 3, 2 };
	Material m;
	m.name = "Piso_Arena";
	m.diffuseColor = color;
	m.specularColor = glm::vec3(0.08f);
	m.shininess = 16.0f;
	return std::make_unique<Model>("piso (generado)", v, idx, m);
}

inline void RegisterArenaAssets(Scene& scene)
{
	const std::string P = "modelos/modelos_propios/";
	const std::string I = "modelos/modelos_importados/";
	
	AssetDesc a;
	// Cerveza de marca blanca
	a.id = "cerveza_E"; a.path = P + "botella_libre/botella_libre.obj";a.pivot = Pivot::Original;
	scene.RegisterAsset(a);

	//Integracion Espectador separado en partes
	const char* piezasE[] = {
		"cuerpo","brazo_derecho","antebrazo_derecho","mano_derecha",
		"brazo_izquierdo","antebrazo_izquierdo","mano_izquierda"
	};

	// Piso de la arena: no existe como modelo, se genera un plano de 44 x 44 m
	a = AssetDesc(); a.id = "piso"; a.path = "(generado)"; a.pivot = Pivot::Original;
	scene.RegisterAsset(a, CreateFloorModel(44.0f, glm::vec3(0.11f, 0.11f, 0.13f)));

	// ---- Modelos propios ----
	// Ring: modelado a escala x3 (plataforma de 18.29 u = 6.10 m). El pivote es la base del
	// piso del ring (no el centro de toda la caja, que incluye la escalera lateral).
	a = AssetDesc(); a.id = "ring"; a.path = P + "Ring/Ring.obj";
	a.unitScale = 1.0f / 3.0f; a.pivotPart = "Piso_Ring";
	scene.RegisterAsset(a);

	// Gradas: peldaños de 2.7 u de alto y 4.72 u de fondo -> 0.45 m x 0.79 m con escala 1/6
	// (barandal de 6 u = 1.0 m). El frente (peldaño más bajo) está en -Z del archivo.
	a = AssetDesc(); a.id = "gradas"; a.path = P + "Gradas/Gradas.obj";
	a.unitScale = 1.0f / 6.0f; a.baseRotation = glm::vec3(0.0f, 180.0f, 0.0f);
	scene.RegisterAsset(a);

	// Rampa y arco: ya está en metros (altura 1.25 m, igual a la del ring) y su origen es el
	// extremo que toca el ring, por eso se respeta el origen. El piso de referencia no se carga.
	a = AssetDesc(); a.id = "rampa"; a.path = P + "rampa_arco/rampa_lucha_libre_texturizada.obj";
	a.pivot = Pivot::Original; a.options.excludeNodes = { "Suelo_Referencia" };
	// Focos del arco: se ven encendidos (solo emisivo, no iluminan)
	a.options.emissiveOverrides = { { "MAT_LED_Calido", glm::vec3(0.7f, 0.5f, 0.2f) }, { "MAT_Lente_Foco", glm::vec3(0.6f, 0.58f, 0.52f) } };
	scene.RegisterAsset(a);

	// Barrera: 1.15 m de alto (altura habitual de una valla de contención) -> escala 0.625.
	// El MTL exportado perdió la textura; se asigna la que el equipo preparó para el modelo.
	a = AssetDesc(); a.id = "barrera"; a.path = P + "barrera/barrera.obj";
	a.unitScale = 0.625f;
	a.options.diffuseOverrides = { { "*", "textures/Textura de acero galvanizado desgastado.png" } };
	scene.RegisterAsset(a);

	// Marcador colgante de 4 pantallas, misma escala que el ring (x3): 2.7 x 2.1 x 3.4 m
	a = AssetDesc(); a.id = "marcador"; a.path = P + "marcador/marcadorBueno.obj";
	a.unitScale = 1.0f / 3.0f; a.pivot = Pivot::Center;
	scene.RegisterAsset(a);

	// Bocina: misma escala que el ring (x3) -> 1.28 m de alto. El MTL no enlaza su textura
	// (Bocina_texture.png, que sí coincide con sus UV). Los conos están en -Z del archivo.
	a = AssetDesc(); a.id = "bocina"; a.path = P + "bocina/bocina.obj";
	a.unitScale = 1.0f / 3.0f; a.baseRotation = glm::vec3(0.0f, 180.0f, 0.0f);
	a.options.diffuseOverrides = { { "*", "Bocina_texture.png" } };
	scene.RegisterAsset(a);

	// Silla metálica: FBX de Blender en centímetros (x100). Escala elegida para que el asiento
	// quede a 0.46 m, igual que el asiento de los espectadores. El respaldo está en +Z del archivo.
	a = AssetDesc(); a.id = "silla"; a.path = P + "silla_metalica/silla_metalica.fbx";
	a.unitScale = 0.46f / 163.0f; a.baseRotation = glm::vec3(0.0f, 180.0f, 0.0f);
	scene.RegisterAsset(a);

	// ---- Modelos importados ----
	// Nota: el Assimp del proyecto no aplica el factor de unidades de FBX; los FBX en
	// centímetros usan unitScale = 0.01. Los personajes cuyas unidades no son reales se
	// escalan por su altura (targetHeight), que es la referencia física más confiable.
	a = AssetDesc(); a.id = "luchador"; a.path = I + "luchador/source/Luchador_SKETCHFAB/Luchador_001.fbx";
	a.targetHeight = 1.85f;
	a.options.excludeNodes = { "Entorno" };   // El archivo incluye una esquina de ring de muestra
	a.options.diffuseOverrides = { { "LuchadorMat", "Luchador_Diffuse.png" } };
	a.options.skinning = true;   // Esqueleto Mixamo: la animación de combate (Space) mueve sus huesos por código
	scene.RegisterAsset(a);

	a = AssetDesc(); a.id = "penta"; a.path = I + "penta/source/Taunt.fbx";
	a.targetHeight = 1.80f;
	a.options.skinning = true;   // El clip "Taunt" que trae el archivo no se usa
	scene.RegisterAsset(a);

	// Árbitro (1.89 m): el FBX original (source/) no tiene esqueleto. Para animar la señal de la
	// tecla T se separó el brazo derecho con su manga (herramientas/separar_arbitro.py) sin cambiar
	// la geometría. Ambas piezas comparten el origen (centro de la base del modelo completo), por
	// eso usan Pivot::Original y se colocan en el mismo punto. El FBX original no se modificó.
	a = AssetDesc(); a.id = "arbitro"; a.path = I + "arbitro-ruper-design/separado/arbitro_cuerpo.fbx";
	a.unitScale = 0.01f; a.pivot = Pivot::Original;
	a.options.diffuseOverrides = { { "*", "../textures/arbitro.png" } };
	scene.RegisterAsset(a);
	a.id = "arbitro_brazo_derecho"; a.path = I + "arbitro-ruper-design/separado/arbitro_brazo_derecho.fbx";
	scene.RegisterAsset(a);

	a = AssetDesc(); a.id = "mascara"; a.path = I + "lucha-libre-mask/source/LuchaLibreOptimized/LuchaLibreOptimized.obj";
	scene.RegisterAsset(a);

	// Cinturón (convertido del .blend con herramientas/convertir_blend.py), 1.26 m de largo
	a = AssetDesc(); a.id = "cinturon"; a.path = I + "cinturon/cinturon_curvado.obj";
	a.pivot = Pivot::Center;
	scene.RegisterAsset(a);

	// Espectadores low-poly sentados (convertidos del .blend). Origen en el centro del asiento
	// a nivel del suelo; asiento a 0.46 m.
	for (char c = 'A'; c <= 'F'; c++)
	{
		a = AssetDesc(); a.id = std::string("espectador_") + c; a.path = I + "espectador/espectador_" + c + ".obj";
		a.pivot = Pivot::Original;
		scene.RegisterAsset(a);
	}

	//Agregamos al espectador separado por partes

	for (const char* pieza : piezasE) {
		a = AssetDesc(); a.id = std::string("E_anim_") + pieza;
		a.path = std::string("modelos/modelos_propios/espectador_E_separado/espectador_E_") + pieza + ".obj";
		a.pivot = Pivot::Original;
		scene.RegisterAsset(a);
	}

	// Cámara de estudio con tripié (1.80 m). El lente apunta a -X en el archivo.
	a = AssetDesc(); a.id = "camara_tv"; a.path = I + "camara/source/camar.fbx";
	a.unitScale = 0.01f; a.baseRotation = glm::vec3(0.0f, 90.0f, 0.0f);
	scene.RegisterAsset(a);

	// Cámara de transmisión low-poly (convertida del .glb), 1.95 m. Lente hacia -X.
	a = AssetDesc(); a.id = "camara_broadcast"; a.path = I + "set-of-four-4-low-poly-cameras/source/camara_broadcast.obj";
	a.baseRotation = glm::vec3(0.0f, 90.0f, 0.0f);
	scene.RegisterAsset(a);

	// Barra de luminarias: con 0.01 mediría 0.93 m y cada lámpara 0.15 m; se usa 0.02 para que
	// las lámparas tengan el tamaño del reflector del proyecto (~0.3 m) y la barra 1.85 m.
	a = AssetDesc(); a.id = "luminaria_techo"; a.path = I + "ceiling-lights/source/PTRCUNWM24VXBNKZ/ASSET.fbx";
	a.unitScale = 0.02f;
	a.options.diffuseOverrides = { { "*", "texture_diff.png" } };
	scene.RegisterAsset(a);

	// Reflector pequeño (0.38 m). Su origen es el punto de montaje superior y se respeta para
	// colgarlo. La lente apunta a +X en el archivo.
	a = AssetDesc(); a.id = "reflector"; a.path = I + "simple-spotlight/source/Spotlight/source/Spotlight.fbx";
	a.unitScale = 0.01f; a.pivot = Pivot::Original; a.baseRotation = glm::vec3(0.0f, -90.0f, 0.0f);
	scene.RegisterAsset(a);

	// Reflector con haz de luz. La lente apunta a +Z del archivo y el haz es un cono irregular de
	// 1.85 m. El haz NO se ilumina: se dibuja como "glow" (Material::glow) con el degradado alfa de
	// TEX_Lightbeam.png (opaco en la lente, transparente en el extremo) y el color de su luz real.
	// Antes se dibujaba como superficie gris iluminada con opacidad fija: de ahí los polígonos grises.
	a = AssetDesc(); a.id = "reflector_haz"; a.path = I + "spotlight/source/Spotlight/Spotlight_Scene.fbx";
	a.unitScale = 0.01f;
	a.options.diffuseOverrides = { { "MAT_SceneProps", "TEX_SceneProps_AL.png" }, { "MAT_Lightbeam", "TEX_Lightbeam.png" } };
	a.options.opacityOverrides = { { "MAT_Lightbeam", 0.55f } };
	a.options.emissiveOverrides = { { "MAT_Lightbeam", glm::vec3(1.0f) } };
	a.options.glowMaterials = { "MAT_Lightbeam" };
	scene.RegisterAsset(a);

	// Juego de tres mesas plegables (7.2 m en fila) para la mesa de comentaristas
	a = AssetDesc(); a.id = "mesas"; a.path = I + "mesas/scene.gltf";
	a.targetHeight = 0.76f;   // Altura estándar de mesa
	scene.RegisterAsset(a);

	a = AssetDesc(); a.id = "bandera"; a.path = I + "mexican-flag/source/FB SP MexFlag.fbx";
	a.unitScale = 0.005f; a.pivot = Pivot::Center;   // 8.2 m en el archivo -> 4.1 x 2.3 m
	a.options.diffuseOverrides = { { "*", "../textures/FB_SP_MexFlag_MexFlag_BaseColor.1001.png" } };
	scene.RegisterAsset(a);

	// Pan de muerto (21 cm) y burrito (18 cm): los OBJ vienen sin MTL, se asigna su textura
	a = AssetDesc(); a.id = "pan"; a.path = I + "pan-de-muerto/source/Pan_De_Muerto_.obj";
	a.unitScale = 0.01f;
	a.options.diffuseOverrides = { { "*", "../textures/Pand_de_Muerto_Diffuse.png" } };
	scene.RegisterAsset(a);

	a = AssetDesc(); a.id = "burrito"; a.path = I + "burrito/source/LP Burrito.obj";
	a.unitScale = 0.015f; a.pivot = Pivot::Center;
	a.options.diffuseOverrides = { { "*", "../textures/Tortilla_basecolor.png" } };
	scene.RegisterAsset(a);
}

// -------------------------------------------------------------------------------------
//  Utilidades de colocación
// -------------------------------------------------------------------------------------

// Giro (grados) para que el frente (+Z) de un objeto en 'from' mire hacia 'to'
inline float FaceYaw(const glm::vec3& from, const glm::vec3& to)
{
	return glm::degrees(atan2f(to.x - from.x, to.z - from.z));
}

// Punto expresado en coordenadas ORIGINALES del archivo de un recurso, llevado al mundo
// a través de la matriz de un objeto (incluye la normalización del recurso).
inline glm::vec3 ModelPointToWorld(const SceneObject& obj, const glm::vec3& filePoint)
{
	return glm::vec3(obj.GetMatrix() * glm::vec4(filePoint, 1.0f));
}

// Altura en el mundo de la cara superior de una parte de un objeto (p.ej. la lona del ring)
inline float PartTopY(Scene& scene, const SceneObject& obj, const std::string& part)
{
	glm::vec3 mn, mx;
	if (!obj.asset || !obj.asset->IsReady() || !obj.asset->model->GetPartBounds(part, mn, mx))
		return 0.0f;
	glm::vec3 wmn, wmx;
	TransformBounds(obj.GetMatrix(), mn, mx, wmn, wmx);
	return wmx.y;
}

// Giro (yaw) e inclinación hacia abajo (pitch, X positivo) para que el frente (+Z) apunte a 'to'
inline glm::vec2 AimAngles(const glm::vec3& from, const glm::vec3& to)
{
	glm::vec3 d = to - from;
	float horizontal = sqrtf(d.x * d.x + d.z * d.z);
	return glm::vec2(glm::degrees(atan2f(-d.y, horizontal)), FaceYaw(from, to));
}

// Pseudoaleatorio determinista: la distribución del público es la misma en cada ejecución
inline float Hash01(int a, int b, int c)
{
	unsigned int h = (unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u ^ (unsigned int)c * 83492791u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

inline std::string RandomSpectator(int a, int b, int c)
{
	int i = (int)(Hash01(a, b, c + 17) * 6.0f) % 6;
	return std::string("espectador_") + (char)('A' + i);
}

// Lado de la arena: dirección desde el ring hacia el lado (n) y eje a lo largo del lado (t)
struct ArenaSide
{
	const char* name;
	glm::vec3 n;
	glm::vec3 t;
	float yawToRing;   // Giro para que el frente mire al ring
};

inline const std::vector<ArenaSide>& GetArenaSides()
{
	static const std::vector<ArenaSide> sides = {
		{ "norte", glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 180.0f },   // +Z (frente a la rampa)
		{ "este", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), -90.0f },    // +X
		{ "oeste", glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), 90.0f },   // -X
		{ "sur", glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.0f },      // -Z (lado de la rampa)
	};
	return sides;
}

// -------------------------------------------------------------------------------------
//  Galería: una instancia de cada recurso en fila, para revisar escalas y orientación
// -------------------------------------------------------------------------------------
inline std::vector<std::string> GetGalleryOrder()
{
	return { "ring", "gradas", "rampa", "barrera", "marcador", "bocina", "silla", "luchador", "penta", "arbitro",
		"arbitro_brazo_derecho", "espectador_A", "espectador_B", "espectador_C", "espectador_D", "espectador_E", "espectador_F",
		"mascara", "cinturon", "camara_tv", "camara_broadcast", "luminaria_techo", "reflector", "reflector_haz",
		"mesas", "bandera", "pan", "burrito" };
}

inline void BuildGallery(Scene& scene, const std::vector<std::string>& ids)
{
	scene.Add("piso", "piso", "arena");
	float x = 0.0f;
	for (const std::string& id : ids)
	{
		ModelAsset* asset = scene.GetAsset(id);
		if (!asset || !asset->IsReady()) continue;
		glm::vec3 size = asset->GetSize();
		x += size.x * 0.5f;
		scene.Add("galeria_" + id, id, "galeria").At(x - asset->localMin.x - size.x * 0.5f, -asset->localMin.y, 0.0f);
		x += size.x * 0.5f + 1.0f;
	}
}

// -------------------------------------------------------------------------------------
//  2. Distribución de la escena
// -------------------------------------------------------------------------------------

// Espectadores sentados en los peldaños de una sección de gradas.
// Las posiciones salen de la geometría del archivo Gradas.obj (unidades originales):
// el peldaño i tiene su borde frontal en z = GRADAS_BORDE[i] y su superficie en y = GRADAS_ALTO[i].
inline void AddBleacherSpectators(Scene& scene, const SceneObject& grada, int section)
{
	const float GRADAS_BORDE[6] = { -14.0f, -9.28f, -4.56f, 0.15f, 4.87f, 9.59f };
	const float GRADAS_ALTO[6] = { 2.52f, 5.22f, 7.91f, 10.61f, 13.3f, 16.0f };
	const float UNIDADES_POR_M = 6.0f;                // Escala de las gradas (1/6)
	const float ASIENTO = 0.46f * UNIDADES_POR_M;     // Altura del asiento de los espectadores
	const float MEDIO_ASIENTO = 0.23f * UNIDADES_POR_M;
	const float PASO = 0.62f * UNIDADES_POR_M;        // Separación entre personas
	const int POR_FILA = 9;

	for (int tier = 0; tier < 6; tier++)
	{
		for (int s = 0; s < POR_FILA; s++)
		{
			if (Hash01(section, tier, s) > Arena::OCUPACION_GRADAS) continue;
			float x = (s - (POR_FILA - 1) * 0.5f) * PASO - 0.82f;   // -0.82: centro X del archivo
			// El frente del asiento queda en el borde del peldaño; el origen del espectador está
			// medio asiento hacia atrás (+Z en el archivo de las gradas) y a nivel de los pies.
			glm::vec3 p(x, GRADAS_ALTO[tier] - ASIENTO, GRADAS_BORDE[tier] + MEDIO_ASIENTO);
			std::string id = "publico_" + std::to_string(section) + "_" + std::to_string(tier) + "_" + std::to_string(s);
			scene.Add(id, RandomSpectator(section, tier, s), "publico").At(ModelPointToWorld(grada, p)).RotY(grada.rotation.y);
		}
	}
}

inline void BuildArena(Scene& scene)
{
	using namespace Arena;
	const glm::vec3 centro(0.0f);

	// ---------------- Piso
	scene.Add("piso", "piso", "arena");

	// ---------------- Zona central: ring como referencia de toda la escena
	SceneObject& ring = scene.Add("ring", "ring", "central").At(0.0f, 0.0f, 0.0f);
	const float lona = PartTopY(scene, ring, "Piso_Ring");   // ~1.22 m

	// Personajes sobre la lona (estáticos por ahora)
	scene.Add("luchador", "luchador", "personajes").At(-1.4f, lona, 0.0f).RotY(90.0f);
	scene.Add("penta", "penta", "personajes").At(1.4f, lona, 0.0f).RotY(-90.0f);
	scene.Add("arbitro", "arbitro", "personajes").At(0.3f, lona, -1.9f).RotY(0.0f);
	// Brazo derecho del árbitro: AnimacionArbitro lo coloca cada cuadro sobre el hombro del cuerpo
	scene.Add("arbitro_brazo_derecho", "arbitro_brazo_derecho", "personajes").At(0.3f, lona, -1.9f).RotY(0.0f);

	// Marcador colgante y barras de luminarias alrededor
	scene.Add("marcador", "marcador", "techo").At(0.0f, MARCADOR_ALTURA, 0.0f);
	for (int i = 0; i < 4; i++)
	{
		const ArenaSide& side = GetArenaSides()[i];
		glm::vec3 p = side.n * LUMINARIAS_DIST + glm::vec3(0.0f, LUMINARIAS_ALTURA, 0.0f);
		scene.Add(std::string("luminaria_") + side.name, "luminaria_techo", "techo").At(p).RotY(side.yawToRing);
	}
	// Reflectores pequeños colgados bajo las esquinas del marcador, apuntando a las esquinas del ring
	for (int i = 0; i < 4; i++)
	{
		glm::vec3 p((i & 1) ? 1.1f : -1.1f, MARCADOR_ALTURA - 1.05f, (i & 2) ? 1.5f : -1.5f);
		glm::vec3 objetivo(p.x * 3.0f, lona, p.z * 2.0f);
		scene.Add("reflector_" + std::to_string(i), "reflector", "techo").At(p).RotY(FaceYaw(p, objetivo));
	}

	// ---------------- Entrada de luchadores (lado -Z)
	SceneObject& rampa = scene.Add("rampa", "rampa", "entrada").At(0.0f, 0.0f, -RAMPA_INICIO);
	// Fin de la rampa (arco) en el mundo, a partir de la caja del modelo
	glm::vec3 rmn, rmx;
	scene.GetWorldBounds(rampa, rmn, rmx);
	const float zArco = rmn.z;   // ~ -14.5
	scene.Add("bandera_entrada", "bandera", "decoracion").At(0.0f, 6.2f, zArco - 1.0f);
	scene.Add("bocina_entrada_izq", "bocina", "entrada").At(-3.3f, 0.0f, zArco - 0.6f).RotY(0.0f);
	scene.Add("bocina_entrada_der", "bocina", "entrada").At(3.3f, 0.0f, zArco - 0.6f).RotY(0.0f);
	// Reflectores con haz sobre la rampa (efectos de entrada). Se cruzan: el más cercano al ring
	// ilumina la salida del arco y el más cercano al arco ilumina el tramo hacia el ring.
	// El pivote es la base del haz (3.4 m, por encima de un luchador sobre la pasarela). El eje del
	// haz ya baja ~16° en el archivo, por eso solo se inclina la diferencia.
	const float HAZ_INCLINACION = 16.0f;
	const float pasarela = 1.25f;   // Altura de la pasarela (igual a la del ring)
	glm::vec2 giroHaz1 = AimAngles(glm::vec3(0.0f, 4.8f, -6.8f), glm::vec3(0.0f, pasarela, zArco + 2.5f));
	glm::vec2 giroHaz2 = AimAngles(glm::vec3(0.0f, 4.8f, -12.2f), glm::vec3(0.0f, pasarela, -8.5f));
	scene.Add("reflector_rampa_1", "reflector_haz", "entrada").At(0.0f, 3.4f, -7.5f).Rot(giroHaz1.x - HAZ_INCLINACION, giroHaz1.y, 0.0f);
	scene.Add("reflector_rampa_2", "reflector_haz", "entrada").At(0.0f, 3.4f, -11.5f).Rot(giroHaz2.x - HAZ_INCLINACION, giroHaz2.y, 0.0f);

	// ---------------- Barreras de protección (cuadro alrededor del ring)
	for (const ArenaSide& side : GetArenaSides())
	{
		for (int k = -3; k <= 3; k++)
		{
			float t = k * BARRERA_PASO;
			// En el lado de la rampa se deja la abertura por donde pasa la pasarela
			if (side.n.z < -0.5f && fabsf(t) < 2.0f) continue;
			glm::vec3 p = side.n * BARRERA_DIST + side.t * t;
			scene.Add(std::string("barrera_") + side.name + "_" + std::to_string(k + 3), "barrera", "barreras")
				.At(p).RotY(side.yawToRing);
		}
	}
	// Barreras a ambos lados de la pasarela, hasta las gradas del lado de la entrada
	for (int k = 0; k < 4; k++)
	{
		float z = -(BARRERA_DIST + 0.9f + k * BARRERA_PASO);
		scene.Add("barrera_rampa_izq_" + std::to_string(k), "barrera", "barreras").At(-2.3f, 0.0f, z).RotY(90.0f);
		scene.Add("barrera_rampa_der_" + std::to_string(k), "barrera", "barreras").At(2.3f, 0.0f, z).RotY(90.0f);
	}

	// ---------------- Primera fila: sillas metálicas con espectadores (fuera de las barreras)
	int chairIndex = 0;
	for (int sideIndex = 0; sideIndex < 4; sideIndex++)
	{
		const ArenaSide& side = GetArenaSides()[sideIndex];
		for (int row = 0; row < 2; row++)
		{
			float dist = row == 0 ? SILLAS_FILA_1 : SILLAS_FILA_2;
			for (float t = -SILLAS_TANGENTE; t <= SILLAS_TANGENTE + 1e-3f; t += SILLAS_PASO)
			{
				if (side.n.z < -0.5f && fabsf(t) < PASILLO_RAMPA + 0.3f) continue;
				glm::vec3 p = side.n * dist + side.t * t;
				std::string suffix = std::to_string(chairIndex++);
				scene.Add("silla_" + suffix, "silla", "primera_fila").At(p).RotY(side.yawToRing);
				if (Hash01(sideIndex, row, chairIndex) < OCUPACION_SILLAS)
					scene.Add("espectador_fila_" + suffix, RandomSpectator(sideIndex, row, chairIndex), "primera_fila")
					.At(p).RotY(side.yawToRing);
			}
		}
	}

	// ---------------- Gradas: 2 secciones por lado, con pasillos libres en las esquinas
	ModelAsset* gradas = scene.GetAsset("gradas");
	float fondoGradas = (gradas && gradas->IsReady()) ? gradas->GetSize().z : 4.875f;
	int section = 0;
	for (const ArenaSide& side : GetArenaSides())
	{
		for (int k = 0; k < 2; k++)
		{
			float t = (k == 0 ? -1.0f : 1.0f) * GRADAS_SECCION;
			if (side.n.z < -0.5f)   // Lado de la rampa: se separan para dejar el pasillo de entrada
				t += (k == 0 ? -1.0f : 1.0f) * PASILLO_RAMPA;
			glm::vec3 p = side.n * (GRADAS_FRENTE + fondoGradas * 0.5f) + side.t * t;
			SceneObject& grada = scene.Add(std::string("gradas_") + side.name + "_" + std::to_string(k), "gradas", "gradas")
				.At(p).RotY(side.yawToRing);
			if (gradas && gradas->IsReady())
				AddBleacherSpectators(scene, grada, section);
			section++;
		}
	}

	// ---------------- Mesa de comentaristas (lado -X) con accesorios
	scene.Add("mesa_comentaristas", "mesas", "mesa").At(MESA_X, 0.0f, 0.0f).RotY(0.0f);
	ModelAsset* mesas = scene.GetAsset("mesas");
	float cubierta = (mesas && mesas->IsReady()) ? mesas->GetSize().y : 0.76f;
	for (int i = 0; i < 3; i++)
		scene.Add("silla_comentarista_" + std::to_string(i), "silla", "mesa").At(MESA_X - 0.9f, 0.0f, -2.2f + 2.2f * i).RotY(90.0f);
	scene.Add("cinturon", "cinturon", "mesa").At(MESA_X, cubierta + 0.25f, -2.4f);
	scene.Add("mascara", "mascara", "mesa").At(MESA_X, cubierta, 2.3f).RotY(90.0f);
	scene.Add("pan_de_muerto", "pan", "mesa").At(MESA_X + 0.1f, cubierta, 0.9f);

	// ---------------- Cámaras de televisión
	glm::vec3 camaraPrincipal(0.0f, 0.0f, 4.8f);
	scene.Add("camara_principal", "camara_tv", "camaras").At(camaraPrincipal).RotY(FaceYaw(camaraPrincipal, centro));
	const glm::vec3 esquinas[3] = { glm::vec3(4.6f, 0.0f, 4.6f), glm::vec3(4.6f, 0.0f, -4.6f), glm::vec3(-4.6f, 0.0f, -4.6f) };
	for (int i = 0; i < 3; i++)
		scene.Add("camara_esquina_" + std::to_string(i), "camara_broadcast", "camaras").At(esquinas[i]).RotY(FaceYaw(esquinas[i], centro));

	// ---------------- Bocinas en los pasillos de las esquinas y bandera sobre las gradas norte
	const glm::vec3 bocinas[2] = { glm::vec3(7.6f, 0.0f, 7.6f), glm::vec3(-7.6f, 0.0f, 7.6f) };
	for (int i = 0; i < 2; i++)
		scene.Add("bocina_esquina_" + std::to_string(i), "bocina", "decoracion").At(bocinas[i]).RotY(FaceYaw(bocinas[i], centro));
	scene.Add("bandera_norte", "bandera", "decoracion").At(0.0f, 6.0f, GRADAS_FRENTE + fondoGradas + 0.4f).RotY(180.0f);

	// ---------------- Burrito volador: sale de un espectador de las gradas norte hacia la lona
	BURRITO_INICIO = glm::vec3(2.0f, 2.4f, GRADAS_FRENTE + 2.5f);
	BURRITO_FIN = glm::vec3(0.0f, lona + 0.05f, 0.0f);
	scene.Add("burrito", "burrito", "accesorios").At(BURRITO_INICIO).Hidden();

	// ---------------- Espectador por partes
	const glm::vec3 posicionE(0.0f, 0.0f, 6.9f);
	scene.Add("E_cerveza_izquierda", "cerveza_E", "primera_fila").At(posicionE).RotY(180.0f);
	scene.Add("E_cerveza_derecha", "cerveza_E", "primera_fila").At(posicionE).RotY(180.0f);
	const char* partesE[] = {
		"cuerpo","brazo_derecho","antebrazo_derecho","mano_derecha",
		"brazo_izquierdo","antebrazo_izquierdo","mano_izquierda"
	};
	for (const char* parte : partesE) {
		const std::string id = std::string("E_anim_") + parte;
		scene.Add(id, id, "primera_fila").At(posicionE).RotY(180.0f);
	}
}

// -------------------------------------------------------------------------------------
//  3. Luces OpenGL (independientes de los modelos de luminarias)
// -------------------------------------------------------------------------------------

// Luz real ligada a un modelo de reflector (los dos de la rampa). Cada cuadro
// (UpdateLightFixtures) su SpotLight toma la posición de la lente y la dirección del eje del
// modelo, y el haz del modelo se tiñe con el color de esa luz. Si se anima el modelo, la luz lo sigue.
struct LightFixture
{
	size_t spotIndex = 0;                   // Índice en LightRig::spotLights
	SceneObject* obj = nullptr;
	glm::vec3 lensFile = glm::vec3(0.0f);   // Centro de la lente (coordenadas del archivo del modelo)
	glm::vec3 aimFile = glm::vec3(0.0f);    // Otro punto del eje del haz (coordenadas del archivo)
	float beamGain = 1.0f;                  // glowColor del haz = color difuso de la luz * beamGain
};

// Medidas del modelo reflector_haz (coordenadas del archivo, en centímetros; lente hacia +Z)
const glm::vec3 REFLECTOR_HAZ_LENTE(0.0f, 58.6f, 17.1f), REFLECTOR_HAZ_EJE(0.0f, 6.6f, 199.6f);

inline void UpdateLightFixtures(LightRig& rig, std::vector<LightFixture>& fixtures)
{
	for (LightFixture& f : fixtures)
	{
		if (!f.obj || f.spotIndex >= rig.spotLights.size()) continue;
		SpotLightParams& spot = rig.spotLights[f.spotIndex];
		spot.position = ModelPointToWorld(*f.obj, f.lensFile);
		spot.direction = ModelPointToWorld(*f.obj, f.aimFile) - spot.position;
		f.obj->glowColor = spot.enabled ? spot.diffuse * f.beamGain : glm::vec3(0.0f);
	}
}

inline void BuildLightRig(LightRig& rig, Scene& scene, std::vector<LightFixture>& fixtures)
{
	// Luz direccional tenue: iluminación general de la arena
	rig.dirLight.direction = glm::vec3(-0.2f, -1.0f, -0.3f);
	rig.dirLight.ambient = glm::vec3(0.22f, 0.22f, 0.26f);
	rig.dirLight.diffuse = glm::vec3(0.35f);
	rig.dirLight.specular = glm::vec3(0.15f);

	// Puntuales: sobre el ring, sobre la entrada y sobre el público
	const glm::vec3 positions[NUMBER_OF_POINT_LIGHTS] = {
		glm::vec3(0.0f, 6.5f, 0.0f), glm::vec3(0.0f, 4.5f, -12.0f), glm::vec3(-9.0f, 6.0f, 6.0f), glm::vec3(9.0f, 6.0f, 6.0f) };
	const char* names[NUMBER_OF_POINT_LIGHTS] = { "ring", "entrada", "publico_oeste", "publico_este" };
	for (int i = 0; i < NUMBER_OF_POINT_LIGHTS; i++)
	{
		PointLightParams& l = rig.pointLights[i];
		l.id = names[i];
		l.position = positions[i];
		l.ambient = glm::vec3(0.03f);
		l.diffuse = glm::vec3(i == 0 ? 0.55f : 0.8f);   // La del ring se suma a los reflectores
		l.specular = glm::vec3(0.5f);
	}

	// Reflectores: uno por cada barra de luminarias, apuntando a la lona.
	// Comparten posición con los modelos "luminaria_*" de BuildArena.
	rig.spotLights.clear();
	for (const ArenaSide& side : GetArenaSides())
	{
		SpotLightParams s;
		s.id = std::string("reflector_") + side.name;
		s.position = side.n * Arena::LUMINARIAS_DIST + glm::vec3(0.0f, Arena::LUMINARIAS_ALTURA - 0.3f, 0.0f);
		s.direction = glm::vec3(0.0f, 1.22f, 0.0f) - s.position;
		s.cutOffDeg = 24.0f;
		s.outerCutOffDeg = 34.0f;
		s.diffuse = glm::vec3(0.45f, 0.43f, 0.40f);
		s.specular = glm::vec3(0.3f);
		rig.spotLights.push_back(s);
	}

	// Reflectores de la rampa: luz de color sobre la pasarela, ligada a los modelos reflector_rampa_*.
	// Su posición y dirección iniciales se reemplazan por las de la lente en UpdateLightFixtures.
	const glm::vec3 colorEntrada(1.0f, 0.22f, 0.62f);   // Rosa mexicano
	const float intensidadRampa = 3.0f;
	fixtures.clear();
	const char* rampa[2] = { "reflector_rampa_1", "reflector_rampa_2" };
	for (int i = 0; i < 2; i++)
	{
		SpotLightParams s;
		s.id = std::string("rampa_") + std::to_string(i + 1);
		s.cutOffDeg = 10.0f;
		s.outerCutOffDeg = 17.0f;
		s.diffuse = colorEntrada * intensidadRampa;
		s.specular = s.diffuse * 0.3f;
		s.linear = 0.045f;
		s.quadratic = 0.0075f;
		rig.spotLights.push_back(s);

		LightFixture f;
		f.spotIndex = rig.spotLights.size() - 1;
		f.obj = scene.Find(rampa[i]);
		f.lensFile = REFLECTOR_HAZ_LENTE;
		f.aimFile = REFLECTOR_HAZ_EJE;
		f.beamGain = 1.0f / intensidadRampa;
		if (f.obj)
			fixtures.push_back(f);
		else
			rig.spotLights.back().enabled = false;   // Galería: no existe el modelo
	}
	UpdateLightFixtures(rig, fixtures);
}

// -------------------------------------------------------------------------------------
//  4. Vistas de cámara (teclas 1-6; R vuelve a la primera)
// -------------------------------------------------------------------------------------
struct CameraView
{
	std::string name;
	glm::vec3 position;
	glm::vec3 target;
};

inline std::vector<CameraView> GetCameraViews()
{
	return {
		{ "General", glm::vec3(15.0f, 11.0f, 19.0f), glm::vec3(0.0f, 1.0f, -1.0f) },
		{ "Ring (camara principal)", glm::vec3(0.0f, 4.2f, 6.2f), glm::vec3(0.0f, 1.6f, -0.5f) },
		{ "Entrada y rampa", glm::vec3(5.5f, 4.0f, -3.0f), glm::vec3(0.0f, 2.0f, -14.0f) },
		{ "Desde el publico", glm::vec3(-12.0f, 3.6f, 0.5f), glm::vec3(0.0f, 1.5f, 0.0f) },
		{ "Cenital", glm::vec3(0.01f, 34.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f) },
		{ "Mesa de comentaristas", glm::vec3(-3.4f, 1.9f, 5.6f), glm::vec3(-4.5f, 0.85f, -0.5f) },
	};
}

// Vistas de la galería: una por objeto, de frente (+Z) y a una distancia según su tamaño
inline std::vector<CameraView> GetGalleryViews(Scene& scene)
{
	std::vector<CameraView> views;
	for (SceneObject& obj : scene.Objects())
	{
		glm::vec3 mn, mx;
		if (obj.group != "galeria" || !scene.GetWorldBounds(obj, mn, mx)) continue;
		glm::vec3 c = (mn + mx) * 0.5f;
		float r = glm::length(mx - mn) * 0.5f;
		views.push_back({ obj.assetId, c + glm::vec3(r * 0.6f, r * 0.7f, r * 1.9f + 0.3f), c });
	}
	return views;
}
